=========================================
Fledge "Python35" C++ Filter plugin
=========================================

A readings data transformation plugin using Python 3.5
embedded interpreter.

NOTE:
Python3.5 development package is required.

Build
-----
To build Fledge "python35" C++ filter plugin:

.. code-block:: console

  $ mkdir build
  $ cd build
  $ cmake ..

- By default the Fledge develop package header files and libraries
  are expected to be located in /usr/include/fledge and /usr/lib/fledge
- If **FLEDGE_ROOT** env var is set and no -D options are set,
  the header files and libraries paths are pulled from the ones under the
  FLEDGE_ROOT directory.
  Please note that you must first run 'make' in the FLEDGE_ROOT directory.

You may also pass one or more of the following options to cmake to override 
this default behaviour:

- **FLEDGE_SRC** sets the path of a Fledge source tree
- **FLEDGE_INCLUDE** sets the path to Fledge header files
- **FLEDGE_LIB sets** the path to Fledge libraries
- **FLEDGE_INSTALL** sets the installation path of Random plugin

NOTE:
 - The **FLEDGE_INCLUDE** option should point to a location where all the Fledge 
   header files have been installed in a single directory.
 - The **FLEDGE_LIB** option should point to a location where all the Fledge
   libraries have been installed in a single directory.
 - 'make install' target is defined only when **FLEDGE_INSTALL** is set

Examples:

- no options

  $ cmake ..

- no options and FLEDGE_ROOT set

  $ export FLEDGE_ROOT=/some_fledge_setup

  $ cmake ..

- set FLEDGE_SRC

  $ cmake -DFLEDGE_SRC=/home/source/develop/Fledge  ..

- set FLEDGE_INCLUDE

  $ cmake -DFLEDGE_INCLUDE=/dev-package/include ..
- set FLEDGE_LIB

  $ cmake -DFLEDGE_LIB=/home/dev/package/lib ..
- set FLEDGE_INSTALL

  $ cmake -DFLEDGE_INSTALL=/home/source/develop/Fledge

  $ cmake -DFLEDGE_INSTALL=/usr/local/fledge

Script restrictions
-------------------

Filter scripts (south and north, including those authored through the
Configurator) run in a restricted environment (PT-HLN-HDP-26A-002, CWE-78).
The script is checked when the filter starts and on every reconfigure, before
any of its code runs:

- an AST check rejects imports of anything outside the allow-list, relative
  and star imports, dunder attribute access (``__class__``, ``__subclasses__``
  ...), references to ``__import__``/``__builtins__``, and to ``eval``,
  ``exec``, ``compile``, ``open``, ``getattr`` and similar builtins;
- the script is then executed with restricted builtins and an ``__import__``
  that enforces the same allow-list at run time.

A rejected script is logged with the reason and line, the filter stays failed
and drops readings (fail closed).

Default allow-list: ``json math cmath time datetime re copy collections
itertools functools string decimal fractions statistics numbers random bisect
heapq struct base64 binascii hashlib hmac enum typing textwrap unicodedata``.
Never importable: ``os subprocess socket sys ctypes importlib shutil io
threading multiprocessing pickle urllib http logging.config`` and others, see
``include/script_sandbox.h``. Scripts that used ``logging``, ``os`` or ``sys``
(for example the old ``examples/scale35.py``) must be changed.

**Decision: the allow-list is configurable per deployment, by the operator
only.** The environment variable ``FLEDGE_PYTHON35_EXTRA_IMPORTS``
(comma separated exact module names) adds modules to the default list. It is
read from the Fledge service environment, not from the filter configuration
category, because that category is editable by the same users who write the
script. The deny-list can not be overridden. Adding modules widens the attack
surface, review each one for ways to reach the OS (``logging`` can write
files, for instance).

This is defence in depth, not a complete sandbox: in-process Python
restrictions are known to be bypassable by a determined author. Keep running
Fledge with least privilege and restrict who can edit filter scripts. Checks
at save time in the Portal/Configurator are a courtesy only; the enforcement
is here.

Tests: the ``PYTHON35.Sandbox*`` cases in ``tests/test_python35.cpp``.
