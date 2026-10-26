"""Identity-only patches for the eight approved private athletes. djultra64.

These patches address existing native rows and never import a PC database or
change target skills, age, height, weight, serve style or natural talent.
"""
import re

from character_formats import Pml, payloads, unpack_zlb


ROSTER = {
    'federer': {'slot': 9, 'id': 15, 'target': 'TukurXX', 'first': 'Roger',
                'last': 'Federer', 'display': 'R. Federer', 'short': 'Fed',
                'left': False, 'country': 'Switzerland'},
    'nadal': {'slot': 10, 'id': 16, 'target': 'Houston', 'first': 'Rafael',
              'last': 'Nadal', 'display': 'R. Nadal', 'short': 'Nad',
              'left': True, 'country': 'Spain'},
    'djokovic': {'slot': 11, 'id': 17, 'target': 'WangXXX', 'first': 'Novak',
                 'last': 'Djokovic', 'display': 'N. Djokovic', 'short': 'Djo',
                 'left': False, 'country': 'Serbia'},
    'agassi': {'slot': 12, 'id': 18, 'target': 'KoenigX', 'first': 'Andre',
               'last': 'Agassi', 'display': 'A. Agassi', 'short': 'Aga',
               'left': False, 'country': 'UnitedStates'},
    'becker': {'slot': 13, 'id': 19, 'target': 'GideXXX', 'first': 'Boris',
               'last': 'Becker', 'display': 'B. Becker', 'short': 'Bec',
               'left': False, 'country': 'Germany'},
    'mcenroe': {'slot': 14, 'id': 20, 'target': 'Riosxxx', 'first': 'John',
                'last': 'McEnroe', 'display': 'J. McEnroe', 'short': 'McE',
                'left': True, 'country': 'UnitedStates'},
    'pioline': {'slot': 15, 'id': 21, 'target': 'Emoryxx', 'first': 'Cedric',
                'last': 'Pioline', 'display': 'C. Pioline', 'short': 'Pio',
                'left': False, 'country': 'France'},
    'haas': {'slot': 16, 'id': 22, 'target': 'NashXXX', 'first': 'Tommy',
             'last': 'Haas', 'display': 'T. Haas', 'short': 'Haa',
             'left': False, 'country': 'Germany'},
}


def _identity(character, target=None, identifier=None):
    if character not in ROSTER:
        raise ValueError('Unknown approved athlete')
    info = ROSTER[character]
    if target is not None and target != info['target'] or identifier is not None and identifier != info['id']:
        raise ValueError('Approved athlete and native roster target differ')
    return info


def country_status(original_db, character):
    """Expose unsupported nationality explicitly instead of inventing an ID.

    Serbia does not exist in the original Xbox country catalog. Its visible
    Country string can be updated, but BirthplaceID/ZoneID retain native backing.
    Any UI using that ID for flags needs separate handling before displaying a
    Serbian flag. The caller must retain this limitation in its receipt.
    """
    info = _identity(character)
    members = payloads(original_db)
    tree = Pml(unpack_zlb(members['media/db/profils(m).pml']))
    matches = []
    for node in tree.node('TCountry').children:
        props = {tree.names.get(k): value for k, value in node.properties}
        if props['ImageFileName'].casefold() == info['country'].casefold():
            matches.append(props)
    if len(matches) > 1:
        raise ValueError('Ambiguous native country')
    result = {'country': info['country'], 'native_country_available': bool(matches)}
    if matches:
        country = matches[0]
        result.update(id=int(country['Id']), country_token=country['Name'], zone_id=int(country['ZoneID']))
    else:
        if info['country'] != 'Serbia':
            raise ValueError('Required native country is unavailable: '+info['country'])
        path = 'TRPlayer/Id='+str(info['id'])
        result.update(country_token=info['country'],
                      preserved_backing_id=int(tree.get(path, 'BirthplaceID')),
                      preserved_backing_zone_id=int(tree.get(path, 'ZoneID')),
                      limitation='No native Serbian country/flag exists; Country text is Serbia, native backing ID remains unchanged. No Serbian flag is claimed.')
    return result


def database_updates(original_db, character, *, target=None, identifier=None):
    """Return disjoint manifest pml_updates; no filesystem mutations occur."""
    info = _identity(character, target, identifier)
    country = country_status(original_db, character)
    members = payloads(original_db)
    updates = []
    for member in ('media/db/playerlist.pml','media/db/players(m).pml','media/db/profils(m).pml'):
        tree = Pml(unpack_zlb(members[member]))
        def update(path, prop, value):
            value = str(value)
            if tree.get(path, prop) != value:
                updates.append({'archive':'Media/Wads/Db.wad', 'member':member,
                                'node':path, 'property':prop, 'value':value})
        if member.endswith('playerlist.pml'):
            for outfit in range(2):
                path = 'TPLayerInfo/DirName=%s%02d' % (info['target'], outfit)
                if tree.get(path,'DirName') != info['target']+'%02d'%outfit or tree.get(path,'IsWoman') != '0':
                    raise ValueError('Native player-list identity differs')
                for prop, value in {'Name':info['display'], 'ShortName':info['short'],
                                    'Available':'1', 'RightHanded':int(not info['left'])}.items():
                    update(path, prop, value)
            continue
        tables = ('TRPlayer','TPlayers') if member.endswith('profils(m).pml') else ('TRPlayer',)
        for table in tables:
            path = '%s/Id=%d' % (table,info['id'])
            if tree.get(path,'CodeName').casefold() != info['target'].casefold():
                raise ValueError('Native database identity differs')
            update(path,'LeftHanded',int(info['left']))
            if country['native_country_available']:
                update(path,'BirthplaceID',country['id'])
            if table == 'TRPlayer':
                for prop,value in {'Name':info['first']+' '+info['last'], 'Firstname':info['first'],
                                    'Lastname':info['last'], 'Country':country['country_token']}.items():
                    update(path,prop,value)
                if country['native_country_available']:
                    update(path,'ZoneID',country['zone_id'])
    return updates


def character_ini(original, character):
    """Rename existing native INI fields; clear a second legacy hair if present.

    Wang/Koenig lack Accessories/Hair entirely. Preserve their native structure
    rather than introducing a creator-only section or rejecting valid INIs.
    """
    info = _identity(character)
    result = original
    for prop,value in {'Name':info['last'],'ShortName':info['short'],'LongName':info['display']}.items():
        pattern = rb'(?m)^('+prop.encode()+rb'[ \t]*=[ \t]*)[^\r\n]*'
        result,count = re.subn(pattern,lambda m:m[1]+value.encode('cp1252'),result)
        if count != 1:
            raise ValueError('Expected one native INI identity property: '+prop)
    result,count = re.subn(rb'(?m)(^\[Accessories\][\s\S]*?^Hair[ \t]*=)[^\r\n]*',lambda m:m[1],result)
    if count > 1:
        raise ValueError('Ambiguous native accessory hair')
    return result
