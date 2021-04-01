# Building release packages

This directory holds the scripts and docker files used to create release
packages. The script `make-package.py` is the entry point.

Basic execution:
```shell
make-package.py --distro <distribution>
```
An example of `<distribution>` is `ubuntu-18.04`. See the `--help` output for
supported distributions and other options.

This will create a directory named `pkg/<distribution>/Release` that contains
packages. Additionally it will create the local docker image
`run-<distribution>` which is the minimal docker image that can be used to run
katana apps.

## Authenticating with docker

The script uses docker to pull the canonical containers for packaging from
Github (unless `--rebuild-docker-image` is set). To enable this you need to
provide Github credentials to the docker tool.

[Get a new access token](https://github.com/settings/tokens) and give it
"read:packages" scope. If you are planning on updating the images, give this
token "write:packages" scope.

Tell docker what the token is:
```shell
GITHUB_TOKEN= #paste the private token from the github WEB UI here
docker login https://docker.pkg.github.com -u <github user> --password-stdin \
         <<< ${GITHUB_TOKEN}
```

## Updating hosted build images

To update build images run `make_package.py` with the `--rebuild-docker-image`
flag. The script will run as it normally does but will also create a local
container image named `build-<distro>`.

Upload this image to Github with:
```shell
# find the image id
docker image ls build-<distro>

# Replace 20210302 below with the date the package was built
docker tag <image ID found above> \
         docker.pkg.github.com/katanagraph/katana-enterprise/build-<distro>:20210302

docker push docker.pkg.github.com/katanagraph/katana-enterprise/build-<distro>:20210302
```

Finally update the appropriate `"build_image"` field in the `distros` dictionary
at the top of `make_package.py` to point to the URL of the uploaded image.
