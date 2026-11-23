# Not an image anyone builds: the shapes of COPY that ci-classify.py has to
# read, for the cases in cases.txt that name this file as the gate image.
FROM scratch AS toolchain
COPY --chmod=0755 fixture/tool.sh /usr/local/bin/tool

FROM toolchain AS bakes
COPY fixture/Cargo.toml fixture/Cargo.lock \
     /tmp/fetch/
COPY fixture/assets/ /tmp/assets/

FROM toolchain AS tier
COPY --from=bakes /opt/baked /opt/baked
COPY --from=ghcr.io/example/image:1 /opt/other /opt/other
