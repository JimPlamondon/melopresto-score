# SPDX-FileCopyrightText: 2026 Jim Plamondon
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the continuous-tuning regression through the common writer."""
import argparse
from pathlib import Path
import sys
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core-root', required=True, type=Path)
args = parser.parse_args()
sys.path.insert(0, str(args.core_root.resolve()))
from tools.tests.test_melo_ready_complete import continuous_tuning_package
from tools.musicxml_to_melo.ready_writer import convert_package
from tools.musicxml_to_melo.notation_oracle import build_oracle
import json
data = Path(__file__).resolve().parents[2] / 'src/importexport/musicxml/tests/data/jims/v5'
package = data / 'continuous-tuning'
package.mkdir(exist_ok=True)
continuous_tuning_package(package)
convert_package(package, data / 'melo-continuous-tuning.musicxml')
(package / 'notation-oracle.json').write_text(json.dumps(build_oracle(package), indent=2, ensure_ascii=False) + '\n')
