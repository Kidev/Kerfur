function (append_env_path VAR VALUE)
    if (DEFINED ENV{${VAR}})
        string(FIND "$ENV{${VAR}}" "${VALUE}" _found)
        if (_found EQUAL -1)
            set(ENV{${VAR}} "$ENV{${VAR}}:${VALUE}")
        endif ()
    else ()
        set(ENV{${VAR}} "${VALUE}")
    endif ()
endfunction ()

function (prepend_env_path VAR VALUE)
    if (DEFINED ENV{${VAR}})
        string(FIND "$ENV{${VAR}}" "${VALUE}" _found)
        if (_found EQUAL -1)
            set(ENV{${VAR}} "${VALUE}:$ENV{${VAR}}")
        endif ()
    else ()
        set(ENV{${VAR}} "${VALUE}")
    endif ()
endfunction ()

function (
    resolve_env_or_var
    INPUT_NAME
    DEFAULT
    OUTPUT_NAME
)
    if (
        DEFINED ${INPUT_NAME}
        AND NOT
            "${${INPUT_NAME}}"
            STREQUAL
            ""
    )
        set(
            ${OUTPUT_NAME}
            "${${INPUT_NAME}}"
            PARENT_SCOPE
        )
    elseif (
        DEFINED ENV{${INPUT_NAME}}
        AND NOT
            "$ENV{${INPUT_NAME}}"
            STREQUAL
            ""
    )
        set(
            ${OUTPUT_NAME}
            "$ENV{${INPUT_NAME}}"
            PARENT_SCOPE
        )
    else ()
        set(
            ${OUTPUT_NAME}
            "${DEFAULT}"
            PARENT_SCOPE
        )
    endif ()
endfunction ()
