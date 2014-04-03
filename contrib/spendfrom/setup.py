from distutils.core import setup
setup(name='mgcspendfrom',
      version='1.0',
      description='Command-line utility for eurocoin "coin control"',
      author='Gavin Andresen',
      author_email='contact@europroject.co',
      requires=['jsonrpc'],
      scripts=['spendfrom.py'],
      )
