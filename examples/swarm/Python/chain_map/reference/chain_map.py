# Frozen reference implementation for Python ChainMap
# Derived from Python's collections.ChainMap

"""
Reference ChainMap behavior derived from Python collections.ChainMap.

A ChainMap groups multiple dicts/mappings together to create a single,
updateable view. Lookups search the mappings successively until a key is found.
Writes, updates, and mutations always apply only to the first mapping.
"""

class ChainMap:
    def __init__(self, *maps):
        # List of mappings in lookup order
        self.maps = list(maps) if maps else [{}]

    def get(self, key, default=None):
        for m in self.maps:
            if key in m:
                return m[key]
        return default

    def __getitem__(self, key):
        for m in self.maps:
            if key in m:
                return m[key]
        raise KeyError(key)

    def __setitem__(self, key, value):
        # Mutations always write to the first/front mapping
        self.maps[0][key] = value

    def __contains__(self, key):
        return any(key in m for m in self.maps)

    def new_child(self, m=None):
        # Create a new ChainMap with a new map (or empty map) at front,
        # followed by all current maps
        if m is None:
            m = {}
        return ChainMap(m, *self.maps)

    @property
    def parents(self):
        # New ChainMap containing all maps except the first
        return ChainMap(*self.maps[1:])
