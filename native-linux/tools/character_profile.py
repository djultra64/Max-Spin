"""Existing Xbox metadata choices and strict optional profile overrides. djultra64."""
from pathlib import Path
import re

from character_formats import Pml, payloads, unpack_zlb, sha

ROOT = Path(__file__).resolve().parents[2]
LIMITS = {'displayName': [1, 64], 'firstName': [1, 64], 'lastName': [1, 64],
          'shortName': [1, 12], 'age': [15, 65], 'heightCm': [120, 230],
          'weightKg': [30, 180]}
TEXT_FIELDS = ('displayName', 'firstName', 'lastName', 'shortName')
NUMBER_FIELDS = ('age', 'heightCm', 'weightKg')
FIELDS = set(TEXT_FIELDS + NUMBER_FIELDS + ('countryId', 'leftHanded', 'backhandStyle', 'playStyle', 'skills'))


def validate_profile(profile, catalog):
    if not isinstance(profile, dict) or not set(profile) <= FIELDS:
        raise ValueError('Unsupported native character profile field')
    normalized = {}
    for key, value in profile.items():
        if key in TEXT_FIELDS:
            if not isinstance(value, str) or not value.strip() or not LIMITS[key][0] <= len(value) <= LIMITS[key][1] or any(ord(c) < 32 for c in value):
                raise ValueError('Invalid native profile text: ' + key)
            try:
                value.encode('cp1252')
            except UnicodeEncodeError as error:
                raise ValueError('Native profile text must use supported CP1252 characters: ' + key) from error
        elif key in NUMBER_FIELDS:
            if type(value) is not int or not LIMITS[key][0] <= value <= LIMITS[key][1]:
                raise ValueError('Invalid native profile integer: ' + key)
        elif key == 'countryId':
            if type(value) is not int or value not in {c['id'] for c in catalog['countries']}:
                raise ValueError('Native profile country is absent from the Xbox catalog')
        elif key == 'leftHanded':
            if type(value) is not bool:
                raise ValueError('Native profile handedness must be boolean')
        elif key in ('backhandStyle', 'playStyle'):
            allowed = (0, 1) if key == 'backhandStyle' else (0, 1, 2)
            if type(value) is not int or value not in allowed:
                raise ValueError('Unsupported native profile style: ' + key)
        elif key == 'skills':
            allowed = {skill['key'] for skill in catalog['skills']}
            if not isinstance(value, dict) or not set(value) <= allowed or any(type(v) is not int or not 0 <= v <= 10 for v in value.values()):
                raise ValueError('Unsupported or out-of-range native profile skill')
            value = dict(value)
        normalized[key] = value
    return normalized


def profile_catalog(*, original_db=None, source_db=None):
    """Catalog uses actual original tables; no invented country/style/skill IDs."""
    if original_db is None:
        original_db = (ROOT / 'assets/Media/Wads/Db.wad').read_bytes()
    if source_db is None:
        source_db = (ROOT.parent / 'character-research/legacy/build/Federer-Creator-Test/Media/Wads/Db.wad').read_bytes()
    original = payloads(original_db)
    source = payloads(source_db)
    definitions = Pml(unpack_zlb(original['media/db/profils(m).pml']))
    players = Pml(unpack_zlb(source['media/db/players(m).pml']))
    player_list = Pml(unpack_zlb(source['media/db/playerlist.pml']))
    def props(tree, node):
        return {tree.names.get(k, '%08x' % k): v for k, v in node.properties}
    countries = [{'id': int(p['Id']), 'name': p['ImageFileName'], 'countryToken': p['Name'], 'zoneId': int(p['ZoneID'])}
                 for p in (props(definitions, n) for n in definitions.node('TCountry').children)]
    skills = []
    majors = {'Forehand', 'Backhand', 'Serve', 'Volley', 'MoveSpeed'}
    labels = {'MoveSpeed': 'Movement speed', 'AccelSpeed': 'Acceleration'}
    for n in definitions.node('TSkills').children:
        p = props(definitions, n)
        if p.get('Minimum') == '0' and p.get('Maximum') == '10':
            name = p['Name']
            label = labels.get(name, re.sub(r'(?<=[a-z])(?=[A-Z])', ' ', name).replace('Fx', 'spin'))
            skills.append({'key': name, 'label': label, 'major': name in majors})
    defaults = {}
    for character, identifier, target in [('federer', 15, 'TukurXX'), ('nadal', 16, 'Houston')]:
        node = 'TRPlayer/Id=%d' % identifier
        p = props(players, players.node(node))
        if p['CodeName'] != target:
            raise ValueError('Preserved profile target identity differs')
        child_skills = {props(players, n)['Name']: props(players, n)['Value'] for n in players.node(node).children}
        row = props(player_list, player_list.node('TPLayerInfo/DirName=' + target + '00'))
        defaults[character] = {'displayName': row['Name'], 'firstName': p['Firstname'], 'lastName': p['Lastname'],
            'shortName': row['ShortName'], 'age': int(p['Age']), 'countryId': int(p['BirthplaceID']),
            'heightCm': int(p['Taille']), 'weightKg': int(p['Poids']), 'leftHanded': p['LeftHanded'] == '1',
            'backhandStyle': int(child_skills['Style']), 'playStyle': int(child_skills['PlayStyle']),
            'skills': {skill['key']: int(child_skills[skill['key']]) for skill in skills}}
    catalog = {'schema': 'maxspin-character-profile-v1', 'version': 1,
        'countries': sorted(countries, key=lambda c: c['name']), 'skills': skills,
        'defaults': defaults, 'limits': LIMITS,
        'choices': {'backhandStyle': [{'value': 0, 'label': 'One-handed'}, {'value': 1, 'label': 'Two-handed'}],
                    'playStyle': [{'value': 0, 'label': 'Offensive baseline'}, {'value': 1, 'label': 'Serve and volley'}, {'value': 2, 'label': 'Baseline'}]},
        'source': {'originalDatabaseSha256': sha(original_db), 'preservedDatabaseSha256': sha(source_db)}}
    for profile in defaults.values():
        validate_profile(profile, catalog)
    return catalog
