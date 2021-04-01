#!/usr/bin/env python3

import click
import atexit
import os
import sys
import shutil
import subprocess

# TODO(amp): Merge with enterprise script of the same name.

# This script does not support Ubuntu 16.04
distros = {
    "ubuntu-18.04": {
        "build_image": "docker.pkg.github.com/katanagraph/katana-enterprise/build-ubuntu-18.04:20210302",
        "cmake_options": [],
        "package_type": "deb",
        "depends": "",
        "base_image": "ubuntu@sha256:bec5a2727be7fff3d308193cfde3491f8fba1a2ba392b7546b43a051853a341d",
        "code_name": "bionic",
    },
    "ubuntu-20.04": {
        "build_image": "docker.pkg.github.com/katanagraph/katana-enterprise/build-ubuntu-20.04:20210302",
        "cmake_options": [],
        "package_type": "deb",
        "depends": "",
        "base_image": "ubuntu@sha256:1515a62dc73021e2e7666a31e878ef3b4daddc500c3d031b35130ac05067abc0",
        "code_name": "focal",
    },
    "centos-8": {
        "build_image": "docker.pkg.github.com/katanagraph/katana-enterprise/build-centos-8:20210302",
        "cmake_options": [],
        "package_type": "rpm",
        "depends": "",
        "base_image": "centos@sha256:dbbacecc49b088458781c16f3775f2a2ec7521079034a7ba499c8b0bb7f86875",
        "code_name": "centos8",
    },
}


@click.command()
@click.option(
    "--distro",
    default="ubuntu-18.04",
    type=click.Choice(distros.keys()),
    help="distribution to package for",
    show_default=True,
)
@click.option(
    "--build-type",
    default="Release",
    type=click.Choice(["Debug", "Release"]),
    help="type of build to package",
    show_default=True,
)
@click.option(
    "--rebuild-docker-image",
    is_flag=True,
    help="rebuild the docker build image rather than pulling it from github",
    show_default=True,
)
@click.option(
    "-o", "--output-dir", default="pkg", help="directory to write the packages to", show_default=True,
)
@click.option("-j", "--jobs", default=4, help="number of jobs (passed to make)", show_default=True)
@click.option("--git-label", help="specify a tag, branch, or SHA to build from")
def package(distro, build_type, output_dir, jobs, git_label, rebuild_docker_image):
    """Create a katana release"""
    run_image = f"run-{distro}"
    deploy_dir = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
    docker_dir = os.path.join(deploy_dir, "package")

    os.makedirs(output_dir, exist_ok=True)

    if git_label:
        repo_dir = os.path.join(os.path.realpath(output_dir), "source")
        git.clone(git_url, repo_dir)
        atexit(shutil.rmtree, repo_dir)
        git.checkout("-C", repo_dir, git_label)
        source_dir = repo_dir
    else:
        # if they gave no label, use this source tree
        source_dir = os.path.dirname(deploy_dir)

    opts = distros[distro]
    is_deb = opts["package_type"] == "deb"

    if rebuild_docker_image:
        build_image = f"build-{distro}"
        build_docker_file = "build-ubuntu.Dockerfile" if is_deb else "build-centos.Dockerfile"
        subprocess.run(
            [
                "docker",
                "build",
                "-f",
                build_docker_file,
                "--build-arg",
                f"BASE_IMAGE={opts['base_image']}",
                "-t",
                build_image,
                ".",
            ],
            cwd=docker_dir,
            check=True,
        )
    else:
        build_image = opts["build_image"]
        subprocess.run(
            ["docker", "pull", build_image,], check=True,
        )

    vol_inspect = subprocess.run(["docker", "volume", "inspect", "katana_package_build"])
    if vol_inspect.returncode == 0:
        subprocess.run(["docker", "volume", "rm", "katana_package_build"], check=True)

    build_script = f"""
        cd /build \
        && conan remove --locks \
        && conan install /source/config --build=missing \
        && cmake -S /source -B . {" ".join(opts["cmake_options"])} \
          -DCMAKE_BUILD_TYPE={build_type} \
          -DKATANA_PACKAGE_TYPE={opts["package_type"]} \
          -DCPACK_SYSTEM_NAME={opts['code_name']} \
          -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
          -DCMAKE_TOOLCHAIN_FILE=conan_paths.cmake \
          -DCPACK_{"DEBIAN_PACKAGE_DEPENDS" if is_deb else "RPM_PACKAGE_REQUIRES"}="{opts['depends']}" \
          -DKATANA_GRAPH_LOCATION=none \
          -DKATANA_STORAGE_BACKEND='s3;gs;azure' \
          -DMPI_HOME=/usr/local/katana \
        && cmake --build . --parallel {jobs} \
        && cpack \
        && /source/deploy/package/post-{opts['package_type']}.sh {distro}
        """

    katana_version = subprocess.check_output(["scripts/version", "show"], cwd=source_dir, encoding="ascii").strip()

    subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "-t",
            "-v",
            f"{source_dir}:/source:ro",
            "-v",
            f"katana_package_ccache_{distro}:/root/.ccache",
            "-v",
            "katana_package_conan_data:/root/.conan/data",
            "-v",
            "katana_package_go:/root/go",
            "-v",
            "katana_package_build:/build",
            "--env",
            f"KATANA_VERSION={katana_version}",
            build_image,
            "bash",
            "-c",
            build_script,
        ],
        check=True,
    )

    subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "-t",
            "-v",
            f"{source_dir}:/source:ro",
            "-v",
            "katana_package_build:/build",
            "--env",
            f"KATANA_VERSION={katana_version}",
            build_image,
            "bash",
            "-c",
            "mkdir -p /build/katana/client && "
            "cp -r /source/katana/client/python /build/katana/client && "
            "cd /build/katana/client/python && "
            "python3 ./setup.py bdist_wheel --dist-dir=/build/pkg",
        ],
        check=True,
    )

    export_dir = os.path.join(os.path.realpath(output_dir), distro, build_type)
    os.makedirs(export_dir, exist_ok=True)

    subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            f"--user={os.getuid()}:{os.getgid()}",
            "-v",
            "katana_package_build:/build:ro",
            "-v",
            f"{export_dir}:/export",
            build_image,
            "bash",
            "-c",
            "cp /pkg/*.* /export; cp /build/pkg/*.* /export",
        ],
        check=True,
    )

    run_docker_file = "run-ubuntu.Dockerfile" if is_deb else "run-centos.Dockerfile"

    # Allow OUTPUT_DIR/EXPORT_DIR to be outside DOCKER_DIR by copying what we need
    # specifically; allows for out-of-tree builds and reduces need to prune
    # unnecessary files from docker context.
    shutil.copy(os.path.join(docker_dir, run_docker_file), export_dir)
    try:
        subprocess.run(
            [
                "docker",
                "build",
                "-f",
                run_docker_file,
                "--build-arg",
                f"PACKAGE_FILES={'*.deb' if is_deb else '*.rpm'}",
                "--build-arg",
                f"BASE_IMAGE={opts['base_image']}",
                "-t",
                run_image,
                ".",
            ],
            cwd=export_dir,
            check=True,
        )
    finally:
        os.unlink(os.path.join(export_dir, run_docker_file))

    print()
    print("Generated artifacts:")
    print("  packages in", export_dir)
    print("  docker image as", run_image)


if __name__ == "__main__":
    package()
