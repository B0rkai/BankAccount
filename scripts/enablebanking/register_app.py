"""Register an Enable Banking application from the CLI and write a config for pull_transactions.py.

Generates an RSA key + self-signed certificate, uploads the certificate, and writes
config.json (or --config) with the returned application id and the key path.

The Control Panel token is read from the EB_CP_TOKEN environment variable (not an argument,
so it stays out of shell history). Get it from a logged-in Control Panel request; it
expires after an hour.

Usage:
  EB_CP_TOKEN=... python register_app.py --name "BankAccount Akos" --redirect https://example.org/
  EB_CP_TOKEN=... python register_app.py --name "BankAccount Viki" --config config-viki.json
"""
import argparse
import json
import os
from datetime import datetime, timedelta, timezone

import requests
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

HERE = os.path.dirname(os.path.abspath(__file__))


def make_key_and_cert(name):
    key = rsa.generate_private_key(public_exponent=65537, key_size=4096)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
    now = datetime.now(timezone.utc)
    cert = (x509.CertificateBuilder()
            .subject_name(subject).issuer_name(subject)
            .public_key(key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(now).not_valid_after(now + timedelta(days=3650))
            .sign(key, hashes.SHA256()))
    key_pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption())
    return key_pem, cert.public_bytes(serialization.Encoding.PEM).decode()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--name", required=True, help="application name shown in the Control Panel")
    p.add_argument("--redirect", action="append", default=None,
                   help="redirect URL, repeatable (default https://example.org/)")
    p.add_argument("--environment", default="PRODUCTION", choices=["PRODUCTION", "SANDBOX"],
                   help="PRODUCTION for real (restricted-mode) data; SANDBOX is simulated")
    p.add_argument("--config", default="config.json", help="config file to write, relative to this script")
    # Required by Enable Banking for PRODUCTION apps; for personal restricted use any sane values do.
    p.add_argument("--description", default="Personal finance import of my own accounts (non-commercial)")
    p.add_argument("--gdpr-email", help="contact email for GDPR requests (required for PRODUCTION)")
    p.add_argument("--privacy-url", default="https://example.org/privacy")
    p.add_argument("--terms-url", default="https://example.org/terms")
    args = p.parse_args()
    if args.environment == "PRODUCTION" and not args.gdpr_email:
        p.error("--gdpr-email is required for PRODUCTION applications")

    token = os.environ.get("EB_CP_TOKEN")
    if not token:
        p.error("set EB_CP_TOKEN to the Control Panel bearer token")

    config_path = os.path.join(HERE, args.config)
    key_file = os.path.splitext(os.path.basename(config_path))[0] + ".pem"
    key_path = os.path.join(HERE, key_file)
    if os.path.exists(config_path) or os.path.exists(key_path):
        raise SystemExit(f"{config_path} or {key_path} already exists; not overwriting")

    key_pem, cert_pem = make_key_and_cert(args.name)
    body = {
        "name": args.name,
        "certificate": cert_pem,
        "environment": args.environment,
        "redirect_urls": args.redirect or ["https://example.org/"],
    }
    if args.environment == "PRODUCTION":
        body.update(description=args.description, gdpr_email=args.gdpr_email,
                    privacy_url=args.privacy_url, terms_url=args.terms_url)
    r = requests.post("https://enablebanking.com/api/applications", json=body, timeout=60,
                      headers={"Authorization": f"Bearer {token}"})
    if r.status_code not in (200, 201):
        raise SystemExit(f"Registration failed {r.status_code}: {r.text}")
    resp = r.json()
    print(json.dumps(resp, indent=2))

    # Save the key only after a successful registration, so a failed call leaves nothing behind.
    with open(key_path, "wb") as f:
        f.write(key_pem)
    app_id = resp.get("app_id") or resp.get("kid") or resp.get("id")
    with open(config_path, "w", encoding="utf-8") as f:
        json.dump({"applicationId": app_id or "<fill in from the response above>", "keyPath": key_file},
                  f, indent=2)
    print(f"Wrote {key_path} and {config_path}")
    if not app_id:
        print("Could not find the application id in the response; copy it into applicationId by hand.")


if __name__ == "__main__":
    main()
