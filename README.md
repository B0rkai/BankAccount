# BankAccount - Persely

Persely (Hungarian for piggy bank) is a home-finance web app: upload MBH Bank and Gránit Bank
exports, categorize them in the browser on any device, and see where the money goes - overview,
ad-hoc queries with charts, client/category/year pages, saved queries and HTML reports.

A C++17 daemon keeps the data (SQLite) and serves a JSON API and the React web app; it ships as
one Docker image. See [CLAUDE.md](CLAUDE.md) for building and the architecture, and
[docs/linux-daemon-deployment.md](docs/linux-daemon-deployment.md) for running it.
