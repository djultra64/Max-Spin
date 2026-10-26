"""Check the release feature contract. Own work: djultra64."""
import argparse
import json
from pathlib import Path
from pack_release import load_feature_parity

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        features = load_feature_parity(args.root)
    except ValueError as error:
        parser.exit(1, str(error) + '\n')
    print(json.dumps({'platforms': ['linux', 'windows'], 'features': features}, indent=2))

if __name__ == '__main__':
    main()
