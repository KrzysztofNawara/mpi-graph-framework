#!/usr/bin/python

from common import *

run_commands(import_modules_string() +
             ["pushd {}".format(get_workdir())] +
             get_cmds() +
             ["popd"])