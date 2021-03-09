import logging
import subprocess
import sys
import os
import tempfile
from distutils.errors import CompileError
from functools import lru_cache
from pathlib import Path
import setuptools
import configparser

import generate_from_jinja

__all__ = ["setup"]


def in_build_call():
    # This is a hack, but WOW setuptools is terrible.
    # We need to check if we are building because otherwise every call to setup.py (even for metadata not build) will
    # cause cython files to be processed. Often the processing happens in tree spewing build files all over the tree.
    return any(a.startswith("build") or a.startswith("install") for a in sys.argv)


def split_cmake_list(s):
    return list(filter(None, s.split(";")))


def unique_list(l):
    return list(dict.fromkeys(l))


class RequirementError(RuntimeError):
    def __str__(self):
        return (
            super(RequirementError, self).__str__()
            + " (Normal Katana builds should use cmake to start the build and NOT directly call setup.py "
            "(cmake calls setup.py as needed). See Python.md or "
            "https://github.com/KatanaGraph/katana/blob/master/Python.md#building-katana-python for Python build "
            "instructions.)"
        )


@lru_cache(maxsize=None)
def require_python_module(module_name, ge_version=None, lt_version=None):
    v_str = ""
    if ge_version:
        v_str += f">={ge_version}"
    if lt_version:
        v_str += f"<{lt_version}"
    if ge_version or lt_version:
        v_str = f" ({v_str})"
    print(f"Checking for Python package '{module_name}'{v_str}...", end="")
    try:
        try:
            mod = __import__(module_name)
        except ImportError:
            raise RequirementError(
                f"'{module_name}' must have version >={ge_version}<{lt_version}, but is not available."
            )
        if ge_version or lt_version:
            import packaging.version

            if hasattr(mod, "__version__"):
                installed_version = packaging.version.parse(mod.__version__)
            else:
                raise RequirementError(
                    f"'{module_name}' must have version >={ge_version}<{lt_version}, but has no __version__ attribute."
                )
            requested_min_version = ge_version and packaging.version.parse(ge_version)
            requested_max_version = lt_version and packaging.version.parse(lt_version)
            if (requested_min_version and requested_min_version > installed_version) or (
                requested_max_version and installed_version >= requested_max_version
            ):
                raise RequirementError(
                    f"'{module_name}' must have version >={ge_version}<{lt_version}, but have version {installed_version}."
                )
    except RequirementError as e:
        print(str(e))
        raise
    else:
        v = getattr(mod, "__version__", "")
        if v:
            v = " " + v
        print(f"Found{v}.")


def _get_build_extension():
    from distutils.core import Distribution
    from distutils.command.build_ext import build_ext

    # Modified from Cython/Build/Inline.py, Apache License Version 2.0
    dist = Distribution()
    # Ensure the build respects distutils configuration by parsing
    # the configuration files
    config_files = dist.find_config_files()
    dist.parse_config_files(config_files)
    build_extension = build_ext(dist)
    # build_extension.verbose = True
    build_extension.finalize_options()
    return build_extension


def test_cython_module(name, cython_code, python_code="", extension_options=None):
    extension_options = extension_options or {}
    require_python_module("cython")
    import Cython.Build
    import Cython.Build.Inline

    print(f"Checking for native {name}...", end="")
    try:
        module_name = f"_test_cython_module_{abs(hash(cython_code))}"
        with tempfile.TemporaryDirectory() as tmpdir:
            tmpdir = Path(tmpdir)
            pyx_file = tmpdir / f"{module_name}.pyx"
            py_file = tmpdir / f"{module_name}_test.py"
            # Modified from Cython/Build/Inline.py, Apache License Version 2.0
            with open(pyx_file, "w") as fh:
                fh.write(cython_code)
            with open(py_file, "w") as fh:
                fh.write(python_code)
            extension = setuptools.Extension(name=module_name, sources=[str(pyx_file)], **extension_options)
            build_extension = _get_build_extension()
            build_extension.extensions = Cython.Build.cythonize(
                [extension], language_level="3", compiler_directives={"binding": True}, quiet=True
            )
            build_extension.build_temp = os.path.dirname(pyx_file)
            build_extension.build_lib = tmpdir
            build_extension.run()

            # module_path = tmpdir / build_extension.get_ext_filename(module_name)
            # module = Cython.Build.Inline.load_dynamic(module_name, str(module_path))
            subprocess.check_call([sys.executable, str(py_file)], cwd=tmpdir, env={"PYTHONPATH": str(tmpdir)})
    except Exception as e:
        print("Failed.")
        raise RequirementError(f"Could not find native {name}.")
    else:
        print("Success.")


def load_lang_config(lang):
    filename = os.environ.get(f"KATANA_{lang}_CONFIG")
    if not filename:
        return dict(compiler=[], linker=[], extra_compile_args=[], extra_link_args=[], include_dirs=[])
    parser = configparser.ConfigParser()
    parser.read(filename, encoding="UTF-8")
    return dict(
        compiler=split_cmake_list(parser.get("build", "COMPILER")),
        extra_compile_args=[f"-D{p}" for p in unique_list(split_cmake_list(parser.get("build", "COMPILE_DEFINITIONS")))]
        + split_cmake_list(parser.get("build", "COMPILE_OPTIONS")),
        extra_link_args=split_cmake_list(parser.get("build", "LINK_OPTIONS")),
        include_dirs=unique_list(split_cmake_list(parser.get("build", "INCLUDE_DIRECTORIES"))),
    )


def find_files(root, prefix, suffix):
    """
    Find files ending with a given suffix in root and its subdirectories and
    return their names relative to root.
    """
    files = []
    for dirpath, _, filenames in os.walk(root):
        for f in filenames:
            if not f.endswith(suffix):
                continue
            relpath = os.path.relpath(dirpath, prefix)
            files.append(Path(relpath) / f)
    return files


def module_name_from_source_name(f, source_root_name):
    parts = []
    for p in f.parents:
        if str(p.name) == source_root_name:
            break
        parts.insert(0, p.name)
    parts.append(f.stem)
    module_name = ".".join(parts)
    return module_name


def process_jinja_file(filename, build_source_root):
    output_file = build_source_root / filename.parent / filename.stem
    output_file.parent.mkdir(parents=True, exist_ok=True)
    for p in filename.parents:
        init_file = build_source_root / p / "__init__.pxd"
        init_file.touch(exist_ok=True)

    # TODO(amp): Ideally this would only process the jinja file when the inputs change. But it's fast and I'm lazy.
    #  https://jinja.palletsprojects.com/en/2.11.x/api/#jinja2.meta.find_referenced_templates
    regenerated = generate_from_jinja.run("python" / filename.parent, filename.name, output_file)
    if regenerated:
        print(f"Processed {filename} with Jinja2.")
    return output_file


def collect_cython_files(source_root):
    """
    Search `source_root` for pyx and pxd files and jinja files which generate them
    and build lists of the existing and generated pyx and pxd files.

    :param source_root: The python source root to search.
    :return: pxd_files, pyx_files
    """
    source_root = Path(source_root)
    pyx_jinja_files = find_files(source_root, source_root.parent, ".pyx.jinja")
    pxd_jinja_files = find_files(source_root, source_root.parent, ".pxd.jinja")

    pxd_files = find_files(source_root, "", ".pxd")
    pyx_files = find_files(source_root, "", ".pyx")

    if not in_build_call():
        print("WARNING: This is not a build call so we are not generating Cython files.", file=sys.stderr)
        return [], []

    for f in pyx_jinja_files + pxd_jinja_files:
        output_file: Path = process_jinja_file(f, source_root.parent)
        if output_file.suffix == ".pyx":
            pyx_files.append(output_file)
        else:
            pxd_files.append(output_file)

    assert all("pyx" in f.name for f in pyx_files)
    assert all("pxd" in f.name for f in pxd_files)

    return pxd_files, pyx_files


def _build_cython_extensions(pyx_files, source_root_name, extension_options):
    cython_extensions = []
    for f in pyx_files:
        module_name = module_name_from_source_name(f, source_root_name)
        cython_extensions.append(setuptools.Extension(module_name, [str(f)], language="c++", **extension_options))

    return cython_extensions


def cythonize(module_list, *, source_root, **kwargs):
    # TODO(amp): Dependencies are yet again repeated here. This needs to come from a central deps list.
    require_python_module("packaging")
    require_python_module("Cython", "0.29.12")
    require_python_module("numpy", "1.10")
    require_python_module("pyarrow", "1.0", "3.0.dev")

    import Cython.Build
    import numpy

    extension_options = load_lang_config("CXX")
    extension_options["include_dirs"].append(numpy.get_include())

    if not extension_options["extra_compile_args"]:
        extension_options["extra_compile_args"] = ["-std=c++17", "-Werror"]

    if extension_options["compiler"]:
        compiler = " ".join(extension_options["compiler"])
        os.environ["CXX"] = compiler
        os.environ["CC"] = compiler

        # Because of odd handling of the linker in setuptools with C++ the
        # compiler and the linker must use the same programs, so build a linker
        # command line using the compiler.
        linker = " ".join(extension_options["compiler"] + ["-pthread", "-shared"])
        os.environ["LDSHARED"] = linker
        os.environ["LDEXE"] = linker

    extension_options["extra_compile_args"].extend(
        [
            # Warnings are common in generated code and hard to fix. Don't make them errors.
            "-Wno-error",
            # Entirely disable some warning that are common in generated code and safe.
            "-Wno-unused-variable",
            "-Wno-unused-function",
            "-Wno-deprecated-declarations",
            # Disable numpy deprecation warning in generated code.
            "-DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION",
        ]
    )
    extension_options.pop("compiler")

    test_extension_options = extension_options.copy()
    test_extension_options.setdefault("extra_link_args", [])
    if not any(s.endswith("/libkatana_galois.so") for s in test_extension_options["extra_link_args"]):
        test_extension_options["extra_link_args"].append("-lkatana_galois")
    test_cython_module(
        "libkatana_galois",
        """
# distutils: language=c++
from katana.cpp.libgalois.Galois cimport setActiveThreads, SharedMemSys
cdef SharedMemSys _katana_runtime
setActiveThreads(1)
    """,
        extension_options=test_extension_options,
    )

    source_root = Path(source_root)
    source_root_name = source_root.name

    pyx_files = list(filter(lambda v: isinstance(v, Path), module_list))
    modules = list(filter(lambda v: not isinstance(v, Path), module_list))
    modules.extend(_build_cython_extensions(pyx_files, source_root_name, extension_options))

    kwargs.setdefault("include_path", [])
    kwargs["include_path"].append(str(source_root))
    kwargs["include_path"].append(numpy.get_include())

    return Cython.Build.cythonize(
        modules,
        nthreads=int(os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", "0")),
        language_level="3",
        compiler_directives={"binding": True},
        **kwargs,
    )


def setup(*, source_dir, package_name, doc_package_name, **kwargs):
    # TODO(amp): Dependencies are yet again repeated here. This needs to come from a central deps list.
    requires = ["pyarrow (<3.0)", "numpy", "numba (>=0.50,<1.0a0)"]

    source_dir = Path(source_dir).absolute()

    pxd_files, pyx_files = collect_cython_files(source_root=source_dir / package_name)

    options = dict(
        version=get_katana_version(),
        name=package_name + "_python",
        packages=setuptools.find_packages(str(source_dir), exclude=("tests",)),
        package_data={"": [str(f) for f in pxd_files]},
        package_dir={"": str(source_dir)},
        tests_require=["pytest"],
        # NOTE: Do not use setup_requires. It doesn't work properly for our needs because it doesn't install the
        # packages in the overall build environment. (It installs them in .eggs in the source tree.)
        requires=requires,
        ext_modules=cythonize(pyx_files, source_root=source_dir),
        zip_safe=False,
        command_options={
            "build_sphinx": {
                "project": ("setup.py", doc_package_name),
                "version": ("katana_setup.py", get_katana_version()),
                "release": ("katana_setup.py", get_katana_version()),
                "copyright": ("katana_setup.py", get_katana_copyright_year()),
                "source_dir": ("katana_setup.py", str(source_dir / "docs")),
                "all_files": ("katana_setup.py", True),
            }
        },
    )
    try:
        from sphinx.setup_command import BuildDoc

        options.update(cmdclass={"build_sphinx": BuildDoc})
    except ImportError:
        pass
    options.update(kwargs)

    setuptools.setup(**options)


def get_katana_version():
    require_python_module("packaging")
    sys.path.append(str((Path(__file__).parent.parent / "scripts").absolute()))
    import katana_version.version

    return str(katana_version.version.get_version())


def get_katana_copyright_year():
    year = os.environ.get("KATANA_COPYRIGHT_YEAR")
    if not year:
        year = "2021"
    return year