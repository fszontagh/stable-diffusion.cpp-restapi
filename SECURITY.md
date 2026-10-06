# Security Policy

## Supported versions

Security fixes land on `main` and in the next release. Only the latest
release and `main` are supported; please upgrade before reporting.

## Reporting a vulnerability

Please report vulnerabilities privately through GitHub:
**Security → Report a vulnerability** on this repository
(<https://github.com/fszontagh/stable-diffusion.cpp-restapi/security/advisories/new>).

Do not open a public issue for security problems.

Include what you can of:

- affected endpoint / component and version or commit
- steps to reproduce or a minimal proof of concept
- impact as you understand it

You should get a first response within a few days. Once a fix is ready it is
released, a GitHub Security Advisory is published, and reporters are credited
unless they ask not to be.

## Deployment notes

- The server binds `0.0.0.0` by default. If it does not need to be reachable
  from other machines, use `--host 127.0.0.1` or put it behind a reverse proxy.
- Enable `auth` and set strong credentials for any network-exposed instance.
  With `auth.allow_public_outputs` (default `true`) generated images under
  `/output` and `/thumb` are readable without logging in; set it to `false`
  if outputs are private.
- API keys (Hugging Face, CivitAI) are stored in the server config file; keep
  it readable only by the service user.
