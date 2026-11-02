"""Check embedded molecular media and required SI section order in a generated DOCX."""
from pathlib import Path
from zipfile import ZipFile
import json
import struct
import sys
import xml.etree.ElementTree as ET

path = Path(sys.argv[1])
ns = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main',
      'a': 'http://schemas.openxmlformats.org/drawingml/2006/main',
      'r': 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'}
with ZipFile(path) as package:
    assert package.testzip() is None, 'ZIP CRC failure'
    for name in package.namelist():
        if name.endswith(('.xml', '.rels')):
            ET.fromstring(package.read(name))
    document = ET.fromstring(package.read('word/document.xml'))
    body = document.find('w:body', ns)
    text = lambda node: ''.join(node.itertext())
    assert text(body[0]) == 'Computational Details'
    indices = {'references': next(i for i,p in enumerate(body) if text(p) == 'References'),
               'figure': next(i for i,p in enumerate(body) if p.find('.//w:drawing',ns) is not None),
               'table': next(i for i,p in enumerate(body) if p.tag.endswith('}tbl')),
               'coordinates': next(i for i,p in enumerate(body) if text(p) == 'Cartesian Coordinates')}
    assert indices['references'] < indices['figure'] < indices['table'] < indices['coordinates']
    relationships = ET.fromstring(package.read('word/_rels/document.xml.rels'))
    targets = {r.attrib['Id']: r.attrib['Target'] for r in relationships}
    media = []
    for blip in document.findall('.//a:blip',ns):
        target = targets[blip.attrib['{'+ns['r']+'}embed']]
        assert not target.startswith(('file:', 'http:', 'https:'))
        png = package.read('word/'+target)
        assert png[:8] == b'\x89PNG\r\n\x1a\n'
        width, height = struct.unpack('>II',png[16:24])
        media.append({'part': target, 'size': [width, height]})
    assert len(media) == 3
    assert [m['size'][0] for m in media] == [3600,2400,2400]
    assert len(document.findall('.//w:tbl',ns)) == 4
    result = {'status':'PASS','media':media,'section_order':indices}
print(json.dumps(result,indent=2))
