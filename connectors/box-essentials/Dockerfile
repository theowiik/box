# box-essentials: the weather and feeds, posting to the box from a server that
# stays on, and their settings page on :8090. One static binary on a
# distroless base (with CA certs).
FROM golang:1.26 AS build
WORKDIR /src
COPY go.mod ./
COPY . .
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /box-essentials . \
    && mkdir /state

FROM gcr.io/distroless/static-debian12
COPY --from=build /box-essentials /box-essentials
# The settings, and which feed items were already shown, so a restart does
# not re-announce them. Mount a volume here (compose.yaml does). uid 1000 owns it.
COPY --from=build --chown=1000:1000 /state /state
ENV BOX_STATE_DIR=/state
USER 1000:1000
VOLUME /state
EXPOSE 8090
ENTRYPOINT ["/box-essentials"]
CMD ["run"]
