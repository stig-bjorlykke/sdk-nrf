#
# Copyright (c) 2024 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

import requests
import json
import sys
from base64 import b64decode, b64encode
from time import sleep
from tlv import TLV
#from hexdump import hexdump

if '--device' in sys.argv:
    # Get device ID from command line
    device_id = sys.argv[sys.argv.index('--device') + 1]
else:
    # Default device ID
    device_id = "8988228066612797879"

token_url = "https://api.1nce.com/management-api/oauth/token"
device_url = "https://api.1nce.com/management-api/v1/integrate/devices/"

fota_urls = [
    "https://dev.testncs.com/stig/mfw_nrf91x1_update_from_2.0.1_to_2.0.1-FOTA-TEST.bin",
    "https://dev.testncs.com/stig/mfw_nrf91x1_update_from_2.0.1-FOTA-TEST_to_2.0.1.bin"
]

fota_files = [
    '/Users/stig/mfw_nrf91x1_update_from_2.0.1_to_2.0.1-FOTA-TEST.bin',
    '/Users/stig/mfw_nrf91x1_update_from_2.0.1-FOTA-TEST_to_2.0.1.bin'
]

headers = {
    "accept": "application/json",
    "content-type": "application/json",
}

payload = {
    "requestMode": "SEND_NOW",
    "sendAttempts": 1,
}

nullbyte = b64encode(b'\x00').decode("utf-8")

def b64(value):
    return b64encode(value.encode()).decode("utf-8")

def authenticate():
    """Authenticate with 1NCE API and save access token to access_token.txt"""
    with open('basic_token.txt', 'r') as file:
        basic_token = file.read().rstrip()
    headers['authorization'] = "Basic " + basic_token

    auth_payload = {
        "grant_type": "client_credentials"
    }

    response = requests.post(token_url, json=auth_payload, headers=headers)
    post_res = json.loads(response.text)

    if not 'access_token' in post_res:
        print(json.dumps(post_res, indent=2))
        return

    with open('access_token.txt', 'w') as file:
        file.write(post_res['access_token'])

    print("Access token saved")

def provision(psk="6E6F72646963736563726574"):
    """Provision a device"""
    with open('access_token.txt', 'r') as file:
        access_token = file.read().rstrip()
    headers['authorization'] = "Bearer " + access_token

    psk_payload = {
        "protocol": "LWM2M",
        "secretKey": psk,
        "format": "HEX"
    }

    response = requests.post(device_url + device_id + "/psk", json=psk_payload, headers=headers)
    post_res = json.loads(response.text)

    if 'deviceId' not in post_res:
        print(json.dumps(post_res, indent=2))
        return

    print("Device " + post_res['deviceId'] + " provisioned")

def do(action, resource, value=None):
    """Perform an action on a resource"""
    with open('access_token.txt', 'r') as file:
        access_token = file.read().rstrip()
    headers['authorization'] = "Bearer " + access_token

    payload['action'] = action
    payload['resourceAddress'] = resource

    if value:
        payload['data'] = value
    elif 'data' in payload:
        del payload['data']

    response = requests.post(device_url + device_id + "/actions/LWM2M", json=payload, headers=headers)
    post_res = json.loads(response.text)

    if not 'id' in post_res:
        print(json.dumps(post_res, indent=2))
        return

    for _ in range(20):
        response = requests.get(device_url + "actions/requests/" + post_res['id'], headers=headers)
        get_res = json.loads(response.text)

        match get_res['status']:
            case "SUCCEEDED":
                print("Request succeeded")
                break
            case "IN_PROGRESS":
                print("Request in progress")
                sleep(1)
            case _:
                print("Request failed: " + get_res['status'])
                break

    if 'resultData' in get_res:
        if 'payload' in get_res['resultData'] and get_res['resultData']['payload']:
            content = b64decode(get_res['resultData']['payload'])
            # print(hexdump(content))
            print(TLV.parse(content))
        else:
            print(json.dumps(get_res['resultData'], indent=2))
    else:
        print(json.dumps(get_res, indent=2))

def fota_url(index=0):
    return fota_urls[index]

def fota_file(index=0):
    with open(fota_files[index], 'rb') as file:
        fota = file.read()
    return b64encode(fota).decode("utf-8")

def read(resource):
    """Read a resource"""
    do("read", resource)

def write(resource, value):
    """Write a value to a resource"""
    do("write", resource, value)

def exec(resource):
    """Execute a resource"""
    do("execute", resource)

def observe_start(resource):
    """Start observing a resource"""
    do("observe-start", resource)

def observe_stop(resource):
    """Stop observing a resource"""
    do("observe-stop", resource)
