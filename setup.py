#!/usr/bin/env python
from distutils.core import setup
setup(
    name = 'ubervisor',
    version = '0.0.2',
    description = 'ubervisor client',
    author = 'Kilian Klimek',
    author_email = 'kilian.klimek@googlemail.com',
    url = 'https://github.com/kil/ubervisor',
    license = 'BSD',
    packages = ['ubervisor'],
    package_dir = {'ubervisor': 'python/'},
)
