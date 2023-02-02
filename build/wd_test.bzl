def wd_test(
        conf,
        srcs = [],
        name = None,
        flags = []):
    """Rule to define tests that run `workerd test` with a particular config.

    Args:
     conf: A .capnp config file defining the test. (`name` will be derived from this if not
        specified.)
     srcs: Files which the .capnp config file may embed. Typically JavaScript files.
     flags: Additional flags to pass to `workerd`. Typically used to pass `--experimental`.
    """
    args = [
        "$(location //src/workerd/server:workerd)",
        "test",
        "$(location {})".format(conf),
    ] + flags

    if name == None:
        name = conf.removesuffix(".capnp")

    _wd_test(
        name = name,
        data = srcs + [conf, "//src/workerd/server:workerd"],
        args = args + flags,
    )

def _wd_test_impl(ctx):
    # Bazel insists that the rule must actually create the executable that it intends to run; it
    # can't just specify some other executable with some args. OK, fine, we'll use a script that
    # just execs its args.
    ctx.actions.write(
        output = ctx.outputs.executable,
        content = "#! /bin/sh\nexec \"$@\"\n",
        is_executable = True,
    )

    return [
        DefaultInfo(
            executable = ctx.outputs.executable,
            runfiles = ctx.runfiles(files = ctx.files.data)
        ),
    ]

_wd_test = rule(
    implementation = _wd_test_impl,
    test = True,
    attrs = {
        "workerd": attr.label(
            allow_single_file = True,
            executable = True,
            cfg = "exec",
            default = "//src/workerd/server:workerd",
        ),
        "flags": attr.string_list(),
        "data": attr.label_list(allow_files = True),
    },
)
