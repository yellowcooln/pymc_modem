"""Discover publishable PlatformIO environments; diagnostic envs are excluded."""
from pathlib import Path
import configparser


def discover_release_envs(platformio_ini: Path) -> list[str]:
    config = configparser.ConfigParser(interpolation=None)
    config.read(platformio_ini, encoding="utf-8")
    envs = [section[4:] for section in config.sections()
            if section.startswith("env:") and
            config.getboolean(section, "custom_release", fallback=True)]
    if not envs:
        raise SystemExit(f"No publishable [env:<name>] blocks found in {platformio_ini}")
    return envs
