#!/usr/bin/env python
import sys
import os
import os.path
import logging
import threading

import PyV8

class cmd(object):
    def __init__(self, *alias):
        self.alias = alias

    def __call__(self, func):
        def wrapped(*args, **kwds):
            return func(*args, **kwds)

        wrapped.alias = list(self.alias)
        wrapped.alias.append(func.__name__)
        wrapped.__name__ = func.__name__
        wrapped.__doc__ = func.__doc__

        return wrapped

class Debugger(PyV8.JSDebugger, threading.Thread):
    log = logging.getLogger("dbg")

    def __init__(self):
        PyV8.JSDebugger.__init__(self)
        threading.Thread.__init__(self, name='dbg')

        self.terminated = False
        self.daemon = True

    def showCopyright(self):
        print "jsdb with PyV8 v%s base on Google v8 engine v%s" % (PyV8.__version__, PyV8.JSEngine.version)
        print "Apache License 2.0 <http://www.apache.org/licenses/LICENSE-2.0>"

    def onMessage(self, msg):
        self.log.info("Debug message: %s", msg)

    def onBreak(self, evt):
        self.log.info("Break event: %s", evt)

    def onException(self, evt):
        self.log.info("Exception event: %s", evt)

    def onNewFunction(self, evt):
        self.log.info("New function event: %s", evt)

    def onBeforeCompile(self, evt):
        self.log.info("Before compile event: %s", evt)

    def onAfterCompile(self, evt):
        self.log.info("After compile event: %s", evt)

    def shell(self):
        while not self.terminated:
            line = raw_input('(jsdb) ').strip()

            if line == '': continue

            try:
                result = PyV8.JSContext.current.eval(line)

                if result:
                    print result
            except KeyboardInterrupt:
                break
            except Exception, e:
                print e

    def debug(self, line):
        args = line.split(' ')
        cmd = args[0]
        args = args[1:]

        func = self.alias.get(cmd)

        if func is None:
            print "Undefined command: '%s'.  Try 'help'." % cmd
        else:
            try:
                func(*args)
            except Exception, e:
                print e

    @property
    def cmds(self):
        if not hasattr(self, '_cmds'):
            self.buildCmds()

        return self._cmds

    @property
    def alias(self):
        if not hasattr(self, "_alias"):
            self.buildCmds()

        return self._alias

    def buildCmds(self):
        self._cmds = []
        self._alias = {}

        for name in dir(self):
            attr = getattr(self, name)

            if callable(attr) and hasattr(attr, 'alias'):
                self._cmds.append(attr)
                
                for name in attr.alias:
                    self._alias[name] = attr
                    
        self.log.info("found %d commands with %d alias", len(self._cmds), len(self._alias))

    @cmd('h', '?')
    def help(self, cmd=None, *args):
        """Print list of commands."""

        if cmd:
            func = self.alias.get(cmd)

            if func is None:
                print "Undefined command: '%s'.  Try 'help'." % cmd
            else:
                print func.__doc__
        else:
            print "List of commands:"
            print

            for func in self.cmds:
                print "%s -- %s" % (func.__name__, func.__doc__)

    @cmd('q')
    def quit(self):
        """Exit jsdb."""
        self.terminated = True

class OS(PyV8.JSClass):
    def system(self, *args):
        return os.system(' '.join(args))

    def chdir(self, path):
        os.chdir(path)

    def mkdirp(self, path):
        os.makedirs(path)

    def rmdir(self, path):
        os.rmdir(path)

    def getenv(self, name):
        return os.getenv(name)

    def setenv(self, name, value):
        os.putenv(name, value)

    def unsetenv(self, name):
        os.unsetenv(name)

    def umask(self, mask):
        return os.umask(mask)

class Shell(PyV8.JSClass):
    os = OS()
    
    def quit(self, code=0):
        sys.exit(code)

    def writeln(self, *args):
        self.write(*args)
        print

    def write(self, *args):
        print ' '.join(args),

    def read(self, filename):
        return open(filename, 'r').read()

    def readline(self):
        return raw_input()

    def load(self, *filenames):
        for filename in filenames:
            PyV8.JSContext.current.eval(open(filename).read())

    def version(self):
        print "PyV8 v%s with Google V8 v%s" % (PyV8.__version__, PyV8.JSEngine.version)

def parse_cmdline():
    from optparse import OptionParser

    parser = OptionParser(usage="Usage: %prog [options] <script file>")

    parser.add_option("-q", "--quiet", action="store_const",
                      const=logging.FATAL, dest="logLevel", default=logging.WARN)
    parser.add_option("-v", "--verbose", action="store_const",
                      const=logging.INFO, dest="logLevel")
    parser.add_option("-d", "--debug", action="store_const",
                      const=logging.DEBUG, dest="logLevel")
    parser.add_option("--log-format", dest="logFormat",
                      default="%(asctime)s %(levelname)s %(name)s %(message)s")
    parser.add_option("--log-file", dest="logFile")

    opts, args = parser.parse_args()

    logging.basicConfig(level=opts.logLevel,
                        format=opts.logFormat,
                        filename=opts.logFile)

    return opts, args

if __name__=='__main__':
    opts, args = parse_cmdline()

    with PyV8.JSContext(Shell()) as ctxt:
        with Debugger() as dbg:
            dbg.showCopyright()

            #dbg.start()

            try:
                dbg.shell()
            except KeyboardInterrupt:
                pass