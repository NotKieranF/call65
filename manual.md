# call65

### Overview

Due to limitations in the design of the 6502 CPU&mdash;too few registers, no stack relative addressing, etc.&mdash;programmers often dedicate a small chunk of the first page of memory to scratch space, to be used for storing intermediate results and passing values between various routines. This becomes difficult to manage as a program's call graph becomes increasingly complex; refactors can become difficult, and subtle bugs can crop up when editing the scratch usage of leaf routines called in many different places. Additionally, ca65 does not allow for reentrant scopes, making it difficult to pass locally scoped identifiers between translation units using a typical header system.

This library intends to remedy the situation by providing a few useful pseudo-directives  to facilitate declaring, allocating, and exporting local variables across translation units using a header system similar to what is common in C.

### Setup

The call65 library requires minimal setup to integrate into a project. All that is needed is a memory area in the zeropage called `SCRATCH` with the `define = yes` attribute for local variables to be placed in. For example, the following linker configuration line will define scratch space at the start of zeropage with 16 bytes of capacity:

`SCRATCH: start = $00, size = $10, type = rw, file = "", define = yes`

Alternatively, `__SCRATCH_START__` and `__SCRATCH_SIZE__` symbols can be manually defined in `call65.inc`.

## Psuedo-directives

### `.routine name`

Analogous to `.proc`.
Starts a new routine scope with the given name.
Cannot be nested, and must be forward declared with `.globalroutine` or `.declareroutine`.

### `.endroutine`

Analogous to `.endproc`. 
Ends the currently open routine.

### `.globalroutine name, [local ...]`

Analogous to `.global`.
Exports a routine and associated local variables if used in the same translation unit as the specified routine, or imports them if not.
Multiple local variables can be specified as a space separated list.

### `.declareroutine name ...`

Forward declares the scope for a routine.
Multiple routines can be specified as a space separated list.
This is necessary, as otherwise ca65 gives a 'scope not found' error when attempting to access the local variables of a routine before actually defining it.

### `.calls routine ...`

Indicates that the current routine calls another.
Multiple routines can be specified as a space separated list.
When needed, a single `.calls` directive should typically come before all `allocatelocal` directives in a particular routine.
See the [advanced tricks](#local-variable-allocation) section for more information.

Specifically, this directive allocates enough scratch space such that subsequent local variable allocations will not overlap with scratch space used by the provided routines.

### `.allocatelocal name, [size]`

Allocates space for a local variable in scratch space.
Size is assumed to be a single byte if left blank.

Local variables can be accessed as `z:routine_name::local_name`.

## Options

The call65 library provides a few options to customize it's behavior. These options can be set either within the `call65.inc` header file, or defined through the command line using the `-D` assembler flag.

### Warning level: `CALL65_WARNING_LEVEL`

The call65 library will emit warnings when certain risky behavior is detected.
* A warning level of `0` supresses all warnings.
* A warning level of `1` warns when a routine directly calls another without declaring so using `.calls`, if `CALL65_SAFE_JSR` is enabled.
* A warning level of `2` additionally warns when a `.calls` directive is encountered after another `.calls` or `.allocatelocal` statement.

By default, the warning level is set to `2`.

### JSR overload: `CALL65_SAFE_JSR`

Defining this symbol to be non-zero enables dependency checking when using the `JSR` opcode within a routine.
A warning will be emitted if `CALL65_WARNING_LEVEL > 0` and the target of the opcode was not previously declared using `.calls`.
This option is enabled by default.

It should be noted that, when enabled, the `ubiquitous_idents` feature is left on throughout assembly, so care should be taken to not accidentally redefine any opcodes.

## Advanced tricks

### Local variable allocation

Occasionally, one will have local variables whose values do not need to persist across calls to another routine.
When scratch space becomes scarce, these are prime candidates for optimization, as they can overlap with the local variables of routines further down the call graph. 

Fortunately, this type of optimization is supported by call65, by shifting the order in which `.allocatelocal` and `.calls` directives are used.
Generally, `.calls` directives can be split and reordered arbitrarily, and `.allocatelocal` directives will only guarantee non-overlapping variable allocation with routines declared up until that point.

For example, the following routine:
```
.routine stem
	.calls leaf
	.allocatelocal foo
	.allocatelocal bar

	lda foo
	clc
	adc bar
	sta bar

	jsr leaf

	lda bar
	rts
.endroutine
```

Can be optimized to:

```
.routine stem
	.allocatelocal foo
	.calls leaf
	.allocatelocal bar

	lda foo
	clc
	adc bar
	sta bar

	jsr leaf

	lda bar
	rts
.endroutine
```

As `foo` can be overwritten by the local variables of `leaf` and all the routines that it calls.