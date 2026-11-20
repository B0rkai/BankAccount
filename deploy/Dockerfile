# The live web service: one image, the C++ daemon serving the built web app and its JSON API.
# Build from the repo root: docker build -f deploy/Dockerfile .

FROM node:26-slim AS web
WORKDIR /web
COPY web/package.json web/package-lock.json ./
RUN npm ci
COPY web/ ./
RUN npm run build

FROM debian:trixie AS daemon
# libwxgtk3.2-dev is the only Debian package carrying wx-config and the wxBase headers; the GUI
# parts it pulls in stay in this build stage.
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ make libwxgtk3.2-dev libssl-dev libgtest-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY Makefile ./
COPY include include
COPY src src
COPY third_party third_party
COPY daemon daemon
# The daemon's own tests run as part of every build: a failing test fails the deploy.
RUN make -j"$(nproc)" all daemon-tests

FROM debian:trixie-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends libwxbase3.2-1t64 libssl3t64 \
    && rm -rf /var/lib/apt/lists/*
# The daemon resolves resources/ (Chart.js/Grid.js for downloaded reports) and
# db/favorite_*.json relative to its working directory.
WORKDIR /app
COPY resources resources
COPY --from=daemon /src/build/linux/bin/daemon /usr/local/bin/bankaccount-daemon
COPY --from=web /web/dist web
# 8300: the tailnet port, behind `tailscale serve`; 8400: the LAN port (password sign-in).
EXPOSE 8300 8400
ENV BANKACCOUNT_DB=/data/BData.baf
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s CMD ["bankaccount-daemon", "--health-check", "8300"]
CMD ["sh", "-c", "exec bankaccount-daemon --db \"$BANKACCOUNT_DB\" --host 0.0.0.0 --tailnet-port 8300 --lan-port 8400 --web-dir /app/web"]
