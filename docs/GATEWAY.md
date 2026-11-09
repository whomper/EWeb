<!-- SPDX-License-Identifier: GPL-2.0-or-later
     Copyright (C) 2026 AERA | Erez Yaary -->
# How EWeb reaches the servers

Direct from the Atari, or through AERA-Gateway (as the README had it until 9 Nov 2026). The
Gateway's own setup is in its README: [gateway/README.md](../gateway/README.md#web-ewebs-gateway).

```
ST, STE:     Atari (EWEBST.PRG) --plain HTTP, home LAN--> Raspberry Pi (webgate.py) --HTTP or HTTPS--> the site
Falcon, TT:  Atari (EWEB.PRG, TLS 1.2, RSA and curves on the DSP) --------------------HTTP or HTTPS--> the site
```

Straight from the Atari, through STinG or MiNTnet: EWeb resolves the name
and connects itself, and for `https://` does TLS 1.2 on the 68030
(BearSSL, ChaCha20-Poly1305 first, which is twice as fast as AES there),
with the server's RSA or ECDSA signatures and the key exchange worked out
on the DSP: on Erez's DFB1 a handshake with Wikipedia's ECDSA certificates
took 8 s the first time and about 3 s after (signatures once checked are
kept in `SIGS.DAT`), where the 68030 alone had taken 86 s. A TT, which has
no DSP, does it all on its 68030, so there the Gateway is the quicker
way. Every certificate is checked against the authorities in `CACERT.PEM`
(Mozilla's list; `ROOTS.DAT` is the same list read in advance), with its
name and dates; there is no way to skip the check. A kept connection serves
the next requests to the same server, and a new one to it resumes the TLS
session, which saves most of the handshake.

Or through the Gateway (its address in Settings > Secure, with Falcon
mode off; always for the ST edition): every page then goes to
AERA-Gateway's web service on a Raspberry Pi, which makes the secure
connection (TLS 1.2 or 1.3, its certificates checked) and hands the page
over your home network. It also makes pictures smaller, turns WebP and the
like into JPEG or PNG, and can send a picture ready for the Atari's screen,
in its own colours, made with EWeb's own decoder and dither. With
`--browser` it opens pages in the Gateway's own browser for snapshots and live
view (CAPTCHAs, pages made by scripts). To set it up, on the Raspberry Pi (Raspberry
Pi OS bullseye or newer, or any Debian or Ubuntu with Python 3.9 or newer),
with the Atari's address for `ATARI_IP`:

```
sudo apt install -y git                  (Raspberry Pi OS Lite only)
git clone https://github.com/whomper/AERA-Gateway
cd AERA-Gateway
./update-pi.sh
sudo gateway/aera-gateway install web --allow ATARI_IP
```

Or, from AERA-Gateway 1.8, every gateway for that Atari in one line:

```
sudo apt install -y git &&
  git clone https://github.com/whomper/AERA-Gateway ~/AERA-Gateway &&
  ~/AERA-Gateway/update-pi.sh --allow ATARI_IP
```

- Add `--browser` to the last line for snapshots and live view (in EWeb,
  View > Through the Gateway's browser and View > Live view): it installs
  Chromium and needs a Raspberry Pi with 2 GB or more.
- `install web` also builds `aerapens` (the pictures for the Atari's screen;
  AERA-Gateway 1.6 and later), installing `gcc` and `make` if they're missing,
  and, once AERA-Gateway has it, `aerajsc`, which compiles pages' scripts for
  EWeb (EWeb 0.41 and later). EWeb runs compiled scripts only from its own cache
  or from the Gateway, trusting the Gateway as it trusts your home network: anyone who can
  answer for the Gateway there could also hand EWeb a script.
- To update: `cd ~/AERA-Gateway && ./update-pi.sh`. To check:
  `sudo aera-gateway status`, or `http://PI_IP:8280/` from a browser that
  doesn't go through the gateway.
- In EWeb: Settings > Secure, the Gateway `PI_IP`, port `8280`; in
  `EWEB.PRG` turn the Direct connection off too (Options > Direct connection,
  or its tick in Settings > Secure): while it is on, pages don't go through the Gateway.

The gateway's own README, with its options and what to do when something
doesn't work: [gateway/README.md](gateway/README.md#web-ewebs-gateway).
