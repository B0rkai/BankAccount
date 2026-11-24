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
COPY tests tests
# The tests run as part of every build: a failing test fails the deploy.
RUN make -j"$(nproc)" all test

FROM debian:trixie-slim
# ca-certificates: the daemon downloads MNB's exchange rates over HTTPS.
RUN apt-get update \
    && apt-get install -y --no-install-recommends libwxbase3.2-1t64 libssl3t64 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
# The daemon resolves resources/ (Chart.js/Grid.js for downloaded reports), changelog.json and
# db/favorite_*.json relative to its working directory.
WORKDIR /app
COPY resources resources
# Settings' "What's new" (GET /api/v1/changelog).
COPY docs/changelog.json changelog.json
COPY --from=daemon /src/build/linux/bin/daemon /usr/local/bin/bankaccount-daemon
# .baf -> SQLite converter, run by deploy/migrate-db.sh with this same image.
COPY --from=daemon /src/build/linux/bin/bankaccount-migrate /usr/local/bin/bankaccount-migrate
COPY --from=web /web/dist web
# 8300: the tailnet port, behind `tailscale serve`; 8400: the LAN port (password sign-in); 8443:
# the LAN page over HTTPS, where Windows Hello unlocks editing (only with a certificate mounted).
EXPOSE 8300 8400 8443
ENV BANKACCOUNT_DB=/data/bankaccount.db
# Snapshots go here when something is mounted; set it empty to turn them off.
ENV BANKACCOUNT_BACKUP_DIR=/backup
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s CMD ["bankaccount-daemon", "--health-check", "8300"]
CMD ["sh", "-c", "exec bankaccount-daemon --db \"$BANKACCOUNT_DB\" --host 0.0.0.0 --tailnet-port 8300 --lan-port 8400 --lan-tls-port 8443 --web-dir /app/web ${BANKACCOUNT_BACKUP_DIR:+--backup-dir \"$BANKACCOUNT_BACKUP_DIR\"}"]
