# Not an image anyone builds: an image that copies the whole repository, for
# the cases in cases.txt that name this file as the gate image.
FROM scratch AS tier
COPY . /src/
