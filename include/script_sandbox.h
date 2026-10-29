#ifndef _SCRIPT_SANDBOX_H
#define _SCRIPT_SANDBOX_H
/*
 * Fledge "Python 3.5" filter: restricted loading of user supplied scripts.
 *
 * Released under the Apache 2.0 Licence
 *
 * User supplied filter scripts must not be able to import modules such as
 * os, subprocess, socket, sys or ctypes (CWE-78, PT-HLN-HDP-26A-002).
 *
 * Scripts are NOT imported with the normal import machinery. Instead the
 * source is read, checked with an AST walk, compiled and executed in a fresh
 * module whose __builtins__ are restricted and whose __import__ only admits
 * modules on an allow-list.
 *
 * The Python source below is run once by the plugin; it defines
 * load_script(path, name) which returns the module or raises
 * ScriptSecurityError (an ImportError) with a message that says what was
 * rejected and where.
 *
 * Operators can add modules to the default allow-list with the environment
 * variable FLEDGE_PYTHON35_EXTRA_IMPORTS (comma separated, exact module
 * names). The deny-list can not be overridden. The allow-list is deliberately
 * not part of the filter configuration category, as that is editable by the
 * same users who author the script.
 */

#define SCRIPT_SANDBOX_LOADER_FUNCTION "load_script"
#define SCRIPT_SANDBOX_EXTRA_IMPORTS_ENV "FLEDGE_PYTHON35_EXTRA_IMPORTS"

static const char *SCRIPT_SANDBOX_SOURCE = R"PYSANDBOX(
import ast
import builtins as _builtins
import types as _types

# Modules that can never be imported, nor referenced as an attribute
DENIED_MODULES = frozenset([
    'os', 'posix', 'nt', 'subprocess', 'socket', 'socketserver', 'ssl',
    'sys', 'ctypes', '_ctypes', 'cffi', 'importlib', 'imp', 'runpy', 'pkgutil',
    'zipimport', 'builtins', '__builtin__', 'shutil', 'pty', 'pexpect',
    'multiprocessing', 'threading', '_thread', 'signal', 'resource', 'fcntl',
    'mmap', 'pickle', 'cPickle', 'marshal', 'shelve', 'code', 'codeop',
    'gc', 'inspect', 'platform', 'pathlib', 'tempfile', 'glob', 'fnmatch',
    'io', 'urllib', 'http', 'ftplib', 'telnetlib', 'smtplib', 'xmlrpc',
    'asyncio', 'select', 'selectors', 'webbrowser', 'pdb', 'trace',
    'sysconfig', 'site', 'distutils', 'setuptools', 'pip', 'posixpath',
    'ntpath', 'logging.config',
])

# Modules a filter script may import by default (exact names)
DEFAULT_ALLOWED_MODULES = frozenset([
    'json', 'math', 'cmath', 'time', 'datetime', 're', 'copy', 'collections',
    'itertools', 'functools', 'string', 'decimal', 'fractions', 'statistics',
    'numbers', 'random', 'bisect', 'heapq', 'struct', 'base64', 'binascii',
    'hashlib', 'hmac', 'enum', 'typing', 'textwrap', 'unicodedata',
])

# Builtins that are made available to scripts
SAFE_BUILTIN_NAMES = (
    'abs', 'all', 'any', 'ascii', 'bin', 'bool', 'bytearray', 'bytes',
    'callable', 'chr', 'classmethod', 'complex', 'dict', 'divmod',
    'enumerate', 'filter', 'float', 'format', 'frozenset', 'hasattr', 'hash',
    'hex', 'id', 'int', 'isinstance', 'issubclass', 'iter', 'len', 'list',
    'map', 'max', 'min', 'next', 'object', 'oct', 'ord', 'pow', 'print',
    'property', 'range', 'repr', 'reversed', 'round', 'set', 'slice',
    'sorted', 'staticmethod', 'str', 'sum', 'super', 'tuple', 'type', 'zip',
    'NotImplemented', 'Ellipsis', 'True', 'False', 'None',
    '__build_class__',
    # exceptions
    'BaseException', 'Exception', 'ArithmeticError', 'AssertionError',
    'AttributeError', 'IndexError', 'KeyError', 'LookupError', 'NameError',
    'NotImplementedError', 'OverflowError', 'RuntimeError', 'StopIteration',
    'TypeError', 'UnicodeError', 'ValueError', 'ZeroDivisionError',
    'ImportError', 'ModuleNotFoundError',
)

# Builtins that are rejected with a clear message when a script refers to them
BLOCKED_BUILTIN_NAMES = frozenset([
    'eval', 'exec', 'compile', 'open', 'input', 'breakpoint', 'getattr',
    'setattr', 'delattr', 'globals', 'locals', 'vars', 'dir', 'memoryview',
    'exit', 'quit', 'help', 'license', 'copyright', 'credits',
])

# Dunder names a script may read
ALLOWED_DUNDER_NAMES = frozenset(['__name__', '__file__', '__doc__'])


class ScriptSecurityError(ImportError):
    """A script uses something that is not permitted in a filter script"""


def _extra_allowed(extra):
    names = set(n.strip() for n in (extra or '').split(',') if n.strip())
    return frozenset(n for n in names if n.split('.')[0] not in DENIED_MODULES
                     and n not in DENIED_MODULES)


def allowed_modules(extra=None):
    return DEFAULT_ALLOWED_MODULES | _extra_allowed(extra)


def _is_denied(name):
    return name in DENIED_MODULES or name.split('.')[0] in DENIED_MODULES


def _import_error(name):
    return ScriptSecurityError(
        "import of module '%s' is not permitted in a filter script" % name)


class _Checker(ast.NodeVisitor):
    def __init__(self, allowed, filename):
        self.allowed = allowed
        self.filename = filename

    def fail(self, node, msg):
        err = ScriptSecurityError(msg)
        err.lineno = getattr(node, 'lineno', None)
        raise err

    def visit_Import(self, node):
        for alias in node.names:
            if _is_denied(alias.name) or alias.name not in self.allowed:
                self.fail(node, "import of module '%s' is not permitted in a "
                          "filter script (line %s)" % (alias.name, node.lineno))

    def visit_ImportFrom(self, node):
        if node.level or not node.module:
            self.fail(node, "relative imports are not permitted in a filter "
                      "script (line %s)" % node.lineno)
        if _is_denied(node.module) or node.module not in self.allowed:
            self.fail(node, "import of module '%s' is not permitted in a "
                      "filter script (line %s)" % (node.module, node.lineno))
        for alias in node.names:
            # Dunder names such as json.__builtins__ expose the real builtins,
            # private names such as random._os expose modules that are denied
            if (alias.name == '*' or _is_denied(alias.name)
                    or alias.name in BLOCKED_BUILTIN_NAMES
                    or alias.name.startswith('_')):
                self.fail(node, "import of '%s' from '%s' is not permitted in a "
                          "filter script (line %s)" %
                          (alias.name, node.module, node.lineno))

    def visit_Attribute(self, node):
        attr = node.attr
        if attr.startswith('__') and attr.endswith('__'):
            self.fail(node, "access to attribute '%s' is not permitted in a "
                      "filter script (line %s)" % (attr, node.lineno))
        if attr in DENIED_MODULES:
            self.fail(node, "access to '%s' is not permitted in a filter "
                      "script (line %s)" % (attr, node.lineno))
        self.generic_visit(node)

    def visit_Name(self, node):
        name = node.id
        if isinstance(node.ctx, ast.Load) and name in BLOCKED_BUILTIN_NAMES:
            self.fail(node, "use of '%s' is not permitted in a filter "
                      "script (line %s)" % (name, node.lineno))
        if isinstance(node.ctx, ast.Load) and name.startswith('__') \
                and name.endswith('__') and name not in ALLOWED_DUNDER_NAMES:
            self.fail(node, "use of name '%s' is not permitted in a filter "
                      "script (line %s)" % (name, node.lineno))
        self.generic_visit(node)


def check_source(source, filename='<script>', allowed=None):
    """Parse source and reject anything not permitted. Returns the code object"""
    if allowed is None:
        allowed = allowed_modules()
    tree = ast.parse(source, filename)
    _Checker(allowed, filename).visit(tree)
    return compile(tree, filename, 'exec')


def _public_facade(module, allowed):
    """A copy of module that only has its public names. Private names often
    refer to modules that are denied, for example random._os, and attribute
    access does not go through __import__. Modules that are not on the
    allow-list are left out as well."""
    facade = _types.ModuleType(module.__name__)
    for key, value in vars(module).items():
        if key.startswith('_'):
            continue
        if isinstance(value, _types.ModuleType) and value.__name__ not in allowed:
            continue
        setattr(facade, key, value)
    return facade


def _make_import(allowed):
    real_import = _builtins.__import__
    facades = {}

    def restricted_import(name, globals=None, locals=None, fromlist=(), level=0):
        if level != 0 or _is_denied(name) or name not in allowed:
            raise _import_error(name)
        module = real_import(name, globals, locals, fromlist, 0)
        # "from pkg import submodule" imports a module that is not on the list
        for item in (fromlist or ()):
            sub = getattr(module, item, None)
            if isinstance(sub, _types.ModuleType) and sub.__name__ not in allowed:
                raise _import_error(sub.__name__)
        facade = facades.get(module.__name__)
        if facade is None:
            facade = facades[module.__name__] = _public_facade(module, allowed)
        return facade

    return restricted_import


def _restricted_builtins(allowed):
    safe = {}
    for name in SAFE_BUILTIN_NAMES:
        safe[name] = getattr(_builtins, name)
    safe['__import__'] = _make_import(allowed)
    return safe


def load_script(path, name, extra_imports=None):
    """Load the script at path as a module called name, in a restricted
    environment. extra_imports is the operator supplied, comma separated,
    list of additional modules"""
    allowed = allowed_modules(extra_imports)
    with _builtins.open(path, 'rb') as f:
        source = f.read()
    code = check_source(source, path, allowed)
    module = _types.ModuleType(name)
    module.__file__ = path
    module.__dict__['__builtins__'] = _restricted_builtins(allowed)
    exec(code, module.__dict__)
    return module
)PYSANDBOX";

#endif
