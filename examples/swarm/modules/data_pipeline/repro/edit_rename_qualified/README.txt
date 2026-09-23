single: (cd single && moss edit rename entity-v1:function:double twice --json) -> ok:true, file rewritten.
split:  (cd split && moss edit rename entity-v1:function:util__double twice --json) -> EDIT_TARGET_AMBIGUOUS
        (cd split && moss edit rename entity-v1:function:util__helper aid --json)   -> EDIT_TARGET_AMBIGUOUS (even a private, unqualified-use helper)
