
class SpecialFuncs(object):
    @classmethod
    def make_body(cls, func):
        key = 'make_body_' + func.funcname
        if key in cls.__dict__:
            return cls.__dict__[key].__get__(None, SpecialFuncs)(func)

    @staticmethod
    def make_body_MemoryContextAllocZeroImpl(func):
        return """
        void *p = malloc(size);
        memset(p, 0, size);
        return p;
        """

    @staticmethod
    def make_body_MemoryContextAllocImpl(func):
        return """
        void *p = malloc(size);
        return p;
        """

    @staticmethod
    def make_body_MemoryContextFreeImpl(func):
        return """
        free(pointer);
        """

    @staticmethod
    def make_body_MemoryContextStrdup(func):
        return """
        return strdup(string);
        """

    @staticmethod
    def make_body_MemoryContextReallocImpl(func):
        return """
        return realloc(pointer, size);
        """

    @staticmethod
    def make_body_MemoryContextAllocZeroAlignedImpl(func):
        return """
        void *p = malloc(size);
        memset(p, 0, size);
        return p;
        """

class ByValStructs(object):

    """These are structs over 32 bit and possibly passed by-value.
       As our mock framework doesn't accept 64 bit integer in some platform,
       we have to treat them specially.
    """
    type_names = set([
            'ArrayTuple',
            'CdbPathLocus',
            'Complex',
            'DbDirNode',
            'DirectDispatchCalculationInfo',
            'FileRepIdentifier_u',
            'FileRepOperationDescription_u',
            'FileRepRelFileNodeInfo_s',
            'FileRepVerifyArguments',
            'FileRepVerifyLogControl_s',
            'FileRepVerifyRequest_s',
            'instr_time',
            'Interval',
            'ItemPointerData',
            'NameData',
            'mpp_fd_set',
            'PGSemaphoreData',
            'PossibleValueSet',
            'PrimaryMirrorModeTransitionArguments',
            'RelFileNode',
            'struct timeval',
            'VariableStatData',
            'XLogRecPtr'
            ])
    @classmethod
    def has(cls, argtype):
        return argtype in cls.type_names
