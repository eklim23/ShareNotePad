FROM golang:1.22-alpine AS build

WORKDIR /src
COPY go.mod ./
COPY main.go ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/sharenotepad-relay .

FROM alpine:3.20

RUN adduser -D -H -s /sbin/nologin app
COPY --from=build /out/sharenotepad-relay /usr/local/bin/sharenotepad-relay

USER app
EXPOSE 8080
ENV SHARENOTEPAD_RELAY_ADDR=:8080

ENTRYPOINT ["/usr/local/bin/sharenotepad-relay"]
