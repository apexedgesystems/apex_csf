# escape=\
# Not an image anyone builds: the shapes of COPY that ci-classify.py has to
# read, for the cases in cases.txt that name this file as the gate image.
FROM scratch AS toolchain
COPY --chmod=0755 fixture/tool.sh /usr/local/bin/tool

FROM toolchain AS bakes
COPY fixture/Cargo.toml fixture/Cargo.lock \
     /tmp/fetch/
COPY fixture/assets/ /tmp/assets/
COPY docs/baked/ tools/ops/rust/baked.bin tools/ops/py/baked.bin src/baked.bin /tmp/by-consumer/
COPY tools/ops/sh/baked.bin tools/analysis/rust/baked.bin tools/analysis/py/baked.bin /tmp/by-consumer/
# The ways a source may be written: the JSON form, a path that is not
# normalized, a tab after the keyword, a wildcard within a path segment (a
# file or a directory), a keyword in lower case, and ADD (a URL is not the
# repository, whatever it is spelled with).
COPY ["fixture/json/table.bin", "fixture/json/other.bin", "/tmp/json/"]
COPY ./fixture/dot/./table.bin /fixture/rooted.bin /tmp/dot/
COPY	fixture/tab.bin /tmp/tab/
COPY fixture/glob/*.lock fixture/glob/?/table.bin fixture/glob/dir-*/ /tmp/glob/
copy fixture/lower.bin /tmp/lower/
# An instruction deferred to the images built on this one.
ONBUILD COPY fixture/onbuild.bin /tmp/onbuild/
# hadolint ignore=DL3020
ADD fixture/added.tar https://example.com/fixture/remote[1].tar /tmp/added/
# A comment inside a continued instruction is not part of it.
COPY fixture/continued.bin \
     # fixture/commented.bin
     fixture/after-comment.bin /tmp/continued/
# escape=`
# The line above is a comment: a directive is read only before anything
# else, and the first line names the default.
# Mounts that take nothing from the repository.
RUN --network=none \
    --mount=type=cache,target=/var/cache/fixture \
    --mount=type=bind,from=toolchain,source=/usr/local/bin,target=/mnt/tools \
    true

FROM toolchain AS tier
COPY --from=bakes /opt/baked /opt/baked
COPY --from=ghcr.io/example/image:1 /opt/other /opt/other
