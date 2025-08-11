import requests
import json
import sys
import time


from pathlib import Path

def load_credentials(file_path=None):
    if file_path is None:
        # Start from the folder containing this script
        script_dir = Path(__file__).resolve().parent
        # Go up 3 levels and into config/
        file_path = script_dir.parent.parent.parent / "config" / "credentials.json"

    with open(file_path, "r") as file:
        creds = json.load(file)
    return creds["clientId"], creds["clientSecret"]


def get_token(client_id, client_secret):
    url = "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"

    response = requests.post(
        url,
        data={"grant_type": "client_credentials"},
        auth=(client_id, client_secret)
    )

    if response.status_code != 200:
        raise Exception(f"Token fetch failed: {response.status_code} {response.text}")

    return response.json()["access_token"]


def get_live_icao24(token):
    url = "https://opensky-network.org/api/states/all"
    headers = {"Authorization": f"Bearer {token}"}
    response = requests.get(url, headers=headers)

    if response.status_code != 200:
        raise Exception(f"Failed to fetch live aircraft: {response.status_code} {response.text}")

    data = response.json()
    states = data.get("states", [])
    if not states:
        raise Exception("No live aircraft found")

    icao24 = states[0][0]
    print(f"Using live aircraft: {icao24}")
    return icao24


def fetch_flights(token, icao24, begin, end):
    url = f"https://opensky-network.org/api/flights/aircraft?icao24={icao24}&begin={begin}&end={end}"
    headers = {"Authorization": f"Bearer {token}"}
    response = requests.get(url, headers=headers)

    if response.status_code == 404:
        raise Exception(f"No recorded flights found for {icao24} in the selected time window (likely not yet landed).")
    elif response.status_code != 200:
        raise Exception(f"Fetch failed: {response.status_code} {response.text}")


    data = response.json()
    if not data:
        raise Exception(f"No flights found for {icao24} between {begin} and {end}.")

    with open("flights.json", "w") as f:
        json.dump(data, f, indent=2)

    print(f"flights.json updated with {len(data)} flight(s).")


if __name__ == "__main__":
    client_id, client_secret = load_credentials()
    token = get_token(client_id, client_secret)

    # Use CLI args if provided
    if len(sys.argv) == 4:
        icao24 = sys.argv[1]
        begin = int(sys.argv[2])
        end = int(sys.argv[3])
    else:
        # Fallback: use a live aircraft and 1-hour time window
        now = int(time.time())
        begin = now - 3600
        end = now
        icao24 = get_live_icao24(token)

    fetch_flights(token, icao24, begin, end)
