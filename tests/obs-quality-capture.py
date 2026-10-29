# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
"""Capture an existing dedicated OBS test source through its local websocket."""
import argparse
import json
from pathlib import Path
import time
import websocket


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=18109)
    parser.add_argument("--source", default="W09 Facecam")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--view", default="key", choices=["key", "input", "foreground", "alpha"])
    parser.add_argument("--inner", type=float)
    parser.add_argument("--refiner", choices=["on", "off"])
    parser.add_argument("--budget", type=int, choices=[256, 384, 512, 768])
    parser.add_argument("--wait", type=float, default=3, help="Seconds to settle after changing settings")
    args = parser.parse_args()
    ws = websocket.create_connection(f"ws://127.0.0.1:{args.port}", timeout=30)
    hello = json.loads(ws.recv())
    if "authentication" in hello["d"]:
        raise RuntimeError("Use a dedicated local test instance with authentication disabled")
    ws.send(json.dumps({"op": 1, "d": {"rpcVersion": 1, "eventSubscriptions": 0}}))
    assert json.loads(ws.recv())["op"] == 2

    def request(kind, data):
        ws.send(json.dumps({"op": 6, "d": {"requestType": kind, "requestId": kind, "requestData": data}}))
        response = json.loads(ws.recv())["d"]
        if not response["requestStatus"]["result"]:
            raise RuntimeError(response)
        return response.get("responseData", {})

    settings = {"obs_view": args.view}
    if args.inner is not None:
        settings["unmix_inner"] = args.inner
    if args.refiner is not None:
        settings["refiner_enabled"] = args.refiner == "on"
        settings["edge_refiner"] = args.refiner == "on"
    if args.budget is not None:
        settings["refiner_max_tiles"] = args.budget
    request("SetSourceFilterSettings", {"sourceName": args.source, "filterName": "CorridorKey", "filterSettings": settings})
    time.sleep(max(0, args.wait))
    request("SaveSourceScreenshot", {"sourceName": args.source, "imageFormat": "png",
                                     "imageFilePath": str(args.output.resolve()), "imageCompressionQuality": -1})
    ws.close()
    print(args.output)


if __name__ == "__main__":
    main()
