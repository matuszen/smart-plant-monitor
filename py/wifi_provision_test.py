#!/usr/bin/env python3

import argparse
import http.client
import sys
import urllib.parse


def send_credentials(host: str, ssid: str, password: str, timeout: float) -> int:
    body = urllib.parse.urlencode({"ssid": ssid, "pass": password})
    headers = {
        "Content-Type": "application/x-www-form-urlencoded",
        "Connection": "close",
        "Host": host,
    }

    conn = http.client.HTTPConnection(host, 80, timeout=timeout)
    try:
        conn.request("POST", "/", body=body, headers=headers)
        resp = conn.getresponse()
        data = resp.read()
        print(f"Response: {resp.status} {resp.reason}")
        if data:
            print(data.decode(errors="replace"))
        return 0 if resp.status == 200 else 1
    except Exception as exc:
        print(f"ERROR: {exc}")
        return 2
    finally:
        conn.close()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Provision Wi-Fi credentials to Smart Plant Monitor AP")
    parser.add_argument("--host", default="192.168.4.1", help="AP IP address (default: 192.168.4.1)")
    parser.add_argument("--ssid", required=True, help="Target Wi-Fi SSID")
    parser.add_argument("--pass", dest="password", default="", help="Target Wi-Fi password (can be empty)")
    parser.add_argument("--timeout", type=float, default=5.0, help="HTTP timeout in seconds")
    args = parser.parse_args(argv)

    return send_credentials(args.host, args.ssid, args.password, args.timeout)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
