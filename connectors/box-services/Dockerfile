# box-services: weather, feeds and visuals, posting to the box from a server
# that stays on. One static binary on a distroless base (with CA certs).
FROM golang:1.26 AS build
WORKDIR /src
COPY go.mod ./
COPY . .
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /box-services . \
    && mkdir /state

FROM gcr.io/distroless/static-debian12
COPY --from=build /box-services /box-services
# Which feed items were already shown, so a restart does not re-announce
# them. Mount a volume here (compose.yaml does). uid 1000 owns it.
COPY --from=build --chown=1000:1000 /state /state
ENV BOX_STATE_DIR=/state
USER 1000:1000
VOLUME /state
ENTRYPOINT ["/box-services"]
CMD ["run"]
