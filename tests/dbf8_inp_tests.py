"""Independent byte round trips and adversarial file-layer syntax tests."""
import base64
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import inp_document as inp

class DocumentTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
    def read(self,text,**kw):
        path=self.root/'main.inp';path.write_bytes(text if isinstance(text,bytes) else text.encode())
        return inp.DeckReader(self.root,**kw).read(path)
    def test_lossless(self):
        (self.root/'part').mkdir();(self.root/'part'/'sub.inp').write_bytes(b'*NODE, INPUT="node data.txt"\r\n*PARAMETER\r\nx=__import__("os")\r\n')
        (self.root/'node data.txt').write_bytes(b'1, 1.D+00, , 3,\r\n')
        raw=b'\xef\xbb\xbf** comment\r\n*HEADING\r\nTitle\r\n*INCLUDE,\r\n** interrupted continuation\r\n INPUT=part/sub.inp\r\n*INCLUDE, INPUT=part/sub.inp\r\n*CUSTOMUNMAPPED, LABEL="a,b"\r\n1,,2,\r\n'
        doc=self.read(raw);self.assertEqual(len(doc['files']),3);self.assertFalse(doc['executable'])
        self.assertEqual(doc['cards'][-1]['data'][0]['fields'],['1','','2',''])
        self.assertEqual(doc['cards'][2]['data'][0]['fields'],['1','1.D+00','','3',''])
        self.assertEqual([r['card'] for r in doc['references']],[1,2,4,5])
        storage=self.root/'doc.json';inp.save(doc,storage)
        with self.assertRaises(FileExistsError):inp.save(doc,storage)
        loaded=inp.load(storage);out=self.root/'export';inp.export(loaded,out)
        for name in doc['files']:self.assertEqual((self.root/name).read_bytes(),(out/name).read_bytes())
        self.assertEqual((out/'main.inp').read_bytes(),raw)
        with self.assertRaises(FileExistsError):inp.export(loaded,out)
    def test_all_catalog_names_preserved_without_semantics(self):
        catalog=inp.default_catalog();self.assertEqual(len(catalog.names),600)
        # INCLUDE needs its external reference; catalog recognition does not validate physics.
        names=[x for x in catalog.names.values() if x!='INCLUDE']
        doc=self.read(''.join('*'+x+'\n' for x in names))
        self.assertEqual([c['keyword'] for c in doc['cards']],names)
        self.assertTrue(all(c['known_keyword'] and c['semantic_status']=='NOT_INTERPRETED' for c in doc['cards']))
        self.assertFalse(doc['semantic_mapping_complete'])
    def test_prefix_csv_and_data_continuation(self):
        c=inp.Catalog(['NODE','NSET','ELEMENT'],True)
        self.assertEqual(c.resolve('EL E'),('ELEMENT',True))
        with self.assertRaises(ValueError):c.resolve('N')
        self.assertEqual(inp.csv_fields('1,"a,b",,'),['1','"a,b"','',''])
        doc=self.read('*ELEMENT\n1,2,\n3,4\n',catalog=c)
        self.assertEqual(len(doc['cards'][0]['data']),2) # keyword-specific continuation not guessed
    def test_bad_syntax(self):
        for text in ['data\n','*\n','*NODE,\n','*NODE, X="bad\n','*NODE\n'+'a'*257+'\n','*NODE\n中文\n','*N\n']:
            with self.subTest(text=text),self.assertRaises(ValueError):self.read(text)
    def test_include_guards(self):
        for text in ['*INCLUDE,INPUT=main.inp\n','*INCLUDE,INPUT=../outside.inp\n','*NODE,INPUT=main.inp\n','*INCLUDE,INPUT=x\n1\n']:
            with self.subTest(text=text),self.assertRaises(ValueError):self.read(text)
        with self.assertRaises(ValueError):self.read('*HEADING\nTitle\n',byte_limit=3)
        with self.assertRaises(ValueError):self.read('*HEADING\n',depth_limit=0)
        (self.root/'data').write_text('*NODE\n',encoding='ascii')
        with self.assertRaises(ValueError):self.read('*NODE,INPUT=data\n')
        with self.assertRaises(ValueError):self.read('*NODE,INPUT=a,INPUT=b\n')
    def test_integrity_and_portable_paths(self):
        doc=self.read('*HEADING\nTitle')
        self.assertEqual(len(doc['diagnostics']),1)
        for name in ['../x','/x','C:/x','dir/source.inp:hidden','dir/NUL.inp','dir/COM1','dir/x.','dir/x ','dir//x','dir\\x','./x']:
            bad=copy.deepcopy(doc);bad['files']={name:next(iter(bad['files'].values()))};bad['entry']=name
            stored=self.root/'bad.json';stored.write_text(json.dumps(bad),encoding='utf-8')
            with self.subTest(name=name),self.assertRaises(ValueError):inp.load(stored)
            with self.subTest(export=name),self.assertRaises(ValueError):inp.export(bad,self.root/'bad-export')
            self.assertFalse((self.root/'bad-export').exists())
        bad=copy.deepcopy(doc);bad['files']['main.inp']['sha256']='0'*64
        stored=self.root/'bad.json';stored.write_text(json.dumps(bad),encoding='utf-8')
        with self.assertRaises(ValueError):inp.load(stored)
        stored.write_text('{"version":1,"version":1}',encoding='utf-8')
        with self.assertRaises(ValueError):inp.load(stored)
        stored.write_text(json.dumps(doc),encoding='utf-8')
        with self.assertRaises(ValueError):inp.load(stored,2)
        for limit in (-2,-1,0,False,1.5):
            with self.subTest(limit=limit),self.assertRaises(ValueError):inp.load(stored,limit)
            with self.subTest(limit=limit),self.assertRaises(ValueError):inp.DeckReader(self.root,byte_limit=limit)
    def test_repeated_input_budget(self):
        (self.root/'sub.inp').write_bytes(b'*HEADING\n'+b'a'*100+b'\n')
        with self.assertRaises(ValueError):self.read('*INCLUDE,INPUT=sub.inp\n'*2,byte_limit=200)
    def test_free_text_duplicates_and_semantic_coverage(self):
        doc=self.read('*HEADING\nAn unmatched " in free text\n*NODE,NSET=A,NSET=B\n1,0,0,0\n*PLASTIC\n42,0\n')
        self.assertIsNone(doc['cards'][0]['data'][0]['fields'])
        self.assertEqual(len(doc['diagnostics']),1)
        self.assertEqual(doc['cards'][1]['coverage']['mapping'],'SUBSET_AVAILABLE_NOT_VALIDATED')
        self.assertEqual(doc['cards'][2]['coverage']['mapping'],'UNMAPPED')
        self.assertTrue(all(not c['coverage']['solve_authorized'] for c in doc['cards']))
    def test_symlink_alias_roundtrip(self):
        (self.root/'child.inp').write_bytes(b'*NODE\n1,0,0,0\n')
        try:(self.root/'alias.inp').symlink_to(self.root/'child.inp')
        except OSError as error:self.skipTest(str(error))
        doc=self.read('*INCLUDE,INPUT=alias.inp\n')
        self.assertIn('alias.inp',doc['files'])
        out=self.root/'symlink-export';inp.export(doc,out)
        again=inp.DeckReader(out).read(out/'main.inp')
        self.assertEqual(doc['files'],again['files'])

if __name__=='__main__':unittest.main()
