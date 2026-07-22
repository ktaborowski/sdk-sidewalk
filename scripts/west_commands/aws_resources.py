# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""AWS IoT Wireless resource management for Sidewalk provisioning."""

from __future__ import annotations

import configparser
import json
import random
import string
import subprocess
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

try:
    import boto3
    from botocore.exceptions import ClientError, NoCredentialsError, ProfileNotFound
except ImportError:
    boto3 = None  # type: ignore

AWS_REGION = "us-east-1"
IOT_WIRELESS_ENDPOINT = "https://api.iotwireless.us-east-1.amazonaws.com"


class AwsProvisionError(Exception):
    pass


def _require_boto3() -> None:
    if boto3 is None:
        raise AwsProvisionError(
            "boto3 is required for west provision. Install with:\n"
            "  pip install -r sidewalk/requirements.txt"
        )


def profile_uses_sso(profile: str) -> bool:
    config = configparser.ConfigParser()
    path = Path.home() / ".aws" / "config"
    if not path.is_file():
        return False
    config.read(path)
    section = profile if profile == "default" else f"profile {profile}"
    return config.has_option(section, "sso_start_url")


def ensure_credentials(profile: str) -> boto3.Session:
    """Return a boto3 session, running aws sso login when needed."""
    _require_boto3()

    def _session() -> boto3.Session:
        return boto3.Session(profile_name=profile, region_name=AWS_REGION)

    try:
        session = _session()
        creds = session.get_credentials()
        if creds is None:
            raise NoCredentialsError()
        creds.get_frozen_credentials()
        return session
    except (NoCredentialsError, ProfileNotFound, ClientError) as exc:
        if profile_uses_sso(profile):
            subprocess.run(
                ["aws", "sso", "login", "--profile", profile],
                check=True,
            )
            session = _session()
            creds = session.get_credentials()
            if creds is None:
                raise AwsProvisionError(
                    "AWS SSO login completed but credentials are still missing.") from exc
            return session
        raise AwsProvisionError(
            f"AWS credentials not available for profile '{profile}'.\n"
            "Configure with 'aws configure' or 'aws configure sso'."
        ) from exc


def _account_id(session: boto3.Session) -> str:
    sts = session.client("sts")
    return sts.get_caller_identity()["Account"]


def _iot_topic_arn(account: str, mqtt_topic: str) -> str:
    topic = mqtt_topic.rstrip("/")
    if topic.endswith("#"):
        topic = topic[:-1] + "*"
    elif not topic.endswith("*"):
        topic = topic + "*"
    return f"arn:aws:iot:{AWS_REGION}:{account}:topic/{topic}"


def ensure_iam_role(session: boto3.Session, role_name: str, mqtt_topic: str) -> str:
    iam = session.client("iam")
    account = _account_id(session)
    topic_arn = _iot_topic_arn(account, mqtt_topic)

    trust_policy = {
        "Version": "2012-10-17",
        "Statement": [
            {
                "Effect": "Allow",
                "Principal": {"Service": "iotwireless.amazonaws.com"},
                "Action": "sts:AssumeRole",
            }
        ],
    }
    publish_policy = {
        "Version": "2012-10-17",
        "Statement": [
            {
                "Effect": "Allow",
                "Action": "iot:Publish",
                "Resource": topic_arn,
            }
        ],
    }

    try:
        response = iam.get_role(RoleName=role_name)
        return response["Role"]["Arn"]
    except ClientError as exc:
        if exc.response["Error"]["Code"] != "NoSuchEntity":
            raise AwsProvisionError(
                f"Failed to get IAM role '{role_name}': {exc}") from exc

    try:
        response = iam.create_role(
            RoleName=role_name,
            AssumeRolePolicyDocument=json.dumps(trust_policy),
            Description="Sidewalk dev destination role (west provision)",
        )
        role_arn = response["Role"]["Arn"]
        iam.put_role_policy(
            RoleName=role_name,
            PolicyName=f"{role_name}PublishPolicy",
            PolicyDocument=json.dumps(publish_policy),
        )
        return role_arn
    except ClientError as exc:
        raise AwsProvisionError(
            f"Failed to create IAM role '{role_name}': {exc}") from exc


def ensure_destination(
    session: boto3.Session,
    name: str,
    role_arn: str,
    mqtt_topic: str,
) -> None:
    client = session.client("iotwireless", endpoint_url=IOT_WIRELESS_ENDPOINT)
    try:
        client.get_destination(Name=name)
        return
    except ClientError as exc:
        if exc.response["Error"]["Code"] != "ResourceNotFoundException":
            raise AwsProvisionError(
                f"Failed to get destination '{name}': {exc}") from exc

    try:
        client.create_destination(
            Name=name,
            ExpressionType="MqttTopic",
            Expression=mqtt_topic,
            Description="Sidewalk dev destination (west provision)",
            RoleArn=role_arn,
        )
    except ClientError as exc:
        raise AwsProvisionError(
            f"Failed to create destination '{name}': {exc}") from exc


def ensure_device_profile(
    session: boto3.Session,
    profile_id: Optional[str],
    profile_name: Optional[str],
) -> Tuple[str, Dict[str, Any]]:
    client = session.client("iotwireless", endpoint_url=IOT_WIRELESS_ENDPOINT)

    if profile_id:
        response = client.get_device_profile(Id=profile_id)
        data = dict(response)
        data.pop("ResponseMetadata", None)
        return profile_id, data

    name = profile_name or (
        "sidewalk_dev_" + "".join(random.choices(string.ascii_lowercase, k=8)))
    try:
        response = client.create_device_profile(Sidewalk={}, Name=name)
        profile_id = response["Id"]
        profile = client.get_device_profile(Id=profile_id)
        data = dict(profile)
        data.pop("ResponseMetadata", None)
        return profile_id, data
    except ClientError as exc:
        raise AwsProvisionError(
            f"Failed to create device profile: {exc}") from exc


def create_wireless_device(
    session: boto3.Session,
    destination_name: str,
    device_profile_id: str,
) -> Tuple[str, Dict[str, Any], Dict[str, Any]]:
    client = session.client("iotwireless", endpoint_url=IOT_WIRELESS_ENDPOINT)

    try:
        response = client.create_wireless_device(
            Type="Sidewalk",
            DestinationName=destination_name,
            Sidewalk={"DeviceProfileId": device_profile_id},
        )
        device_id = response["Id"]
        device = client.get_wireless_device(
            Identifier=device_id,
            IdentifierType="WirelessDeviceId",
        )
        profile = client.get_device_profile(Id=device_profile_id)
    except ClientError as exc:
        raise AwsProvisionError(
            f"Failed to create wireless device: {exc}") from exc

    device_data = dict(device)
    device_data.pop("ResponseMetadata", None)
    profile_data = dict(profile)
    profile_data.pop("ResponseMetadata", None)
    return device_id, device_data, profile_data


def save_json(path: Path, data: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2)
