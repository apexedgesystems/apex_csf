# Not an image anyone builds: inputs written in forms ci-classify.py does not
# read. cases.txt names this file under @refused, and every COPY, ADD and RUN
# here must be refused.
FROM scratch AS bakes
ARG SOURCE=fixture/variable.bin
COPY ${SOURCE} /tmp/
COPY fixture/$SOURCE /tmp/
COPY "fixture/quoted.bin" /tmp/
COPY 'fixture/single.bin' /tmp/
COPY fixture/escaped\*.bin /tmp/
COPY fixture/class[ab].bin /tmp/
COPY ["fixture/variable/${SOURCE}", "/tmp/"]
COPY [1, "/tmp/"]
COPY fixture/../../outside.bin /tmp/
# hadolint ignore=DL3020
ADD ${SOURCE} /tmp/
RUN --mount=type=bind,target=/src true
RUN --mount=target=/src true
RUN --mount=type=cache,target=/var/cache/fixture \
    --mount=type=bind,source=fixture/mounted.bin,target=/tmp/mounted.bin \
    true
COPY <<END /tmp/inline.txt
inline
END
