#!/usr/bin/env python

from chunk import Chunk
import logging
import struct

class Parser(object):
  ChunkAliasMap = {}

  def __init__(self, kind):
    self._kind = kind
    self._chunks = []

  def loadFile(self, filename):
    with open(filename) as iff:
      chunk = Chunk(iff)

      logging.info('Reading file "%s"' % filename)

      if chunk.getname() == 'FORM' and chunk.read(4) == self._kind:
        iff.seek(12)

        while True:
          try:
            chunk = Chunk(iff)
          except EOFError:
            break

          name = chunk.getname()
          size = chunk.getsize()
          data = chunk.read()

          logging.debug('Encountered %s chunk of size %d' % (name, size))

          self._chunks.append(self._parseChunk(name, data))
      else:
        logging.error('File %s is not of IFF/%s type.' % (filename, self._kind))
        return False

    return True

  def _parseChunk(self, name, data):
    orig_name = name

    for alias, names in self.ChunkAliasMap.items():
      if name in names:
        name = alias

    handler = getattr(self, 'handle%s' % name, None)

    if handler:
      data = handler(data)
    else:
      logging.warning('No handler for %s chunk.' % orig_name)

    return (orig_name, data)