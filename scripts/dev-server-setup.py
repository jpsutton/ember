#!/usr/bin/env python3
"""Finishes the setup wizard of a fresh development Jellyfin server and adds
the dev-media.sh libraries. Safe to run again: it skips what is done."""

import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://localhost:8096"
CLIENT = 'MediaBrowser Client="ember-dev-setup", Device="workstation", DeviceId="ember-dev-setup", Version="1"'


def request(method, path, body=None, token=None, query=None):
    url = BASE + path
    if query:
        url += "?" + urllib.parse.urlencode(query, doseq=True)
    data = json.dumps(body).encode() if body is not None else None
    auth = CLIENT + (f', Token="{token}"' if token else "")
    req = urllib.request.Request(url, data=data, method=method, headers={"Authorization": auth})
    if data is not None:
        req.add_header("Content-Type", "application/json")
    for attempt in range(90):
        try:
            with urllib.request.urlopen(req, timeout=30) as response:
                text = response.read()
                return json.loads(text) if text else None
        except urllib.error.HTTPError as error:
            # 503 while the server is still starting up.
            if error.code != 503 or attempt == 89:
                raise
            time.sleep(2)


def wait_for_server():
    for _ in range(120):
        try:
            return request("GET", "/System/Info/Public")
        except (urllib.error.URLError, ConnectionError):
            time.sleep(1)
    sys.exit("server did not come up")


def login(user, password):
    return request("POST", "/Users/AuthenticateByName", {"Username": user, "Pw": password})["AccessToken"]


def main():
    info = wait_for_server()
    print(f"server {info.get('ServerName')} version {info.get('Version')}")
    if not info.get("StartupWizardCompleted"):
        request("POST", "/Startup/Configuration",
                {"UICulture": "en-US", "MetadataCountryCode": "US", "PreferredMetadataLanguage": "en"})
        request("GET", "/Startup/User")
        request("POST", "/Startup/User", {"Name": "ember", "Password": "ember"})
        request("POST", "/Startup/RemoteAccess", {"EnableRemoteAccess": True, "EnableAutomaticPortMapping": False})
        request("POST", "/Startup/Complete")
        print("setup wizard completed")

    token = login("ember", "ember")

    config = request("GET", "/System/Configuration", token=token)
    # The test clips are a few minutes long; Jellyfin only keeps resume
    # points for items longer than MinResumeDurationSeconds (300 by default).
    if not config.get("QuickConnectAvailable") or config.get("MinResumeDurationSeconds") != 30:
        config["QuickConnectAvailable"] = True
        config["MinResumeDurationSeconds"] = 30
        request("POST", "/System/Configuration", config, token=token)
        print("enabled Quick Connect, resume points from 30 s")

    users = {u["Name"]: u for u in request("GET", "/Users", token=token)}
    if "guest" not in users:
        guest = request("POST", "/Users/New", {"Name": "guest", "Password": "guest"}, token=token)
        print(f"created user guest ({guest['Id']})")

    folders = {f["Name"] for f in request("GET", "/Library/VirtualFolders", token=token)}
    for name, kind, path in (("Movies", "movies", "/media/movies"), ("Shows", "tvshows", "/media/shows")):
        if name not in folders:
            request("POST", "/Library/VirtualFolders",
                    {"LibraryOptions": {"PathInfos": [{"Path": path}]}},
                    token=token, query={"name": name, "collectionType": kind, "refreshLibrary": "true"})
            print(f"added library {name}")

    # Wait for the scan, then put the Blender films in a collection.
    for _ in range(180):
        tasks = request("GET", "/ScheduledTasks", token=token)
        if not any(t.get("State") == "Running" for t in tasks):
            break
        time.sleep(2)
    me = request("GET", "/Users/Me", token=token)
    collections = request("GET", "/Items", token=token,
                          query={"userId": me["Id"], "IncludeItemTypes": "BoxSet", "Recursive": "true"})["Items"]
    if not any(c["Name"] == "Blender Open Movies" for c in collections):
        movies = request("GET", "/Items", token=token,
                         query={"userId": me["Id"], "IncludeItemTypes": "Movie", "Recursive": "true"})["Items"]
        blender = [m["Id"] for m in movies if m["Name"] in
                   ("Big Buck Bunny", "Sintel", "Elephants Dream", "Tears of Steel", "Cosmos Laundromat",
                    "Spring", "Agent 327: Operation Barbershop")]
        if blender:
            request("POST", "/Collections", token=token,
                    query={"Name": "Blender Open Movies", "Ids": ",".join(blender)})
            print(f"created collection with {len(blender)} films")

    counts = request("GET", "/Items/Counts", token=token)
    print(f"library: {counts.get('MovieCount')} movies, {counts.get('SeriesCount')} series, "
          f"{counts.get('EpisodeCount')} episodes")


if __name__ == "__main__":
    main()
