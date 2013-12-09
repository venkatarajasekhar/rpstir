###
# Program: runTests.py
# Author(s): Brian Buchanan, John Slivka, Elijah Batkoski
# Description: Runs all tests that are listed in the testScripts list
###
import unittest
import seeddb as seed

## Add all test scripts that need to run into this list
testScripts = [
	'swingpointTest',
	'swingpointUtilityTest'
	]

suite = unittest.TestSuite()

for t in testScripts:
	try:
		mod = __import__(t,globals(),locals(),['suite'])
		suitefn = getattr(mod, 'suite')
		suite.addTest(suitefn())
	except:
		suite.addTest(unittest.defaultTestLoader.loadTestsFromName(t))

seed.dbdown()
seed.dbup()

## Runs all tests listed above
unittest.TextTestRunner().run(suite)
