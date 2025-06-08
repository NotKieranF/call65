; Copyright (C) 2025 Kieran F
;
; Permission is hereby granted, free of charge, to any person obtaining a copy of this
; software and associated documentation files (the "Software"), to deal in the Software
; without restriction, including without limitation the rights to use, copy, modify,
; merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
; permit persons to whom the Software is furnished to do so.
; 
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
; INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
; PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
; OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
; SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

; Due to limitations in the design of the 6502 CPU--too few registers, no stack relative
; addressing, etc.--programmers often dedicate a small chunk of the first page of memory
; to scratch space, to be used for storing intermediate results and passing values between
; various routines. This becomes difficult to manage as a program's call graph becomes
; increasingly complex; refactors can become difficult, and subtle bugs can crop up when
; editing the scratch usage of leaf routines called in many different places. Additionally,
; ca65 does not allow for reentrant scopes, making it difficult to pass locally scoped
; identifiers between translation units.
;
; This library intends to remedy the situation by providing a few useful pseudo-directives 
; to facilitate declaring, allocating, and exporting local variables across translation
; units using a header system similar to C. This, unfortunately, includes the requirement
; of forward declaration, even among routines within a single translation unit
;
; List of pseudo-directives:
; - .routine
; - .endroutine
; - .globalroutine
; - .declareroutine
; - .calls
; - .allocatelocal

.ifndef	::CALL65_H
::CALL65_H = 1
.feature	leading_dot_in_identifiers +, string_escapes +

; Configure warning level for risky behavior (0..2)
; ::CALL65_WARNING_LEVEL = 2

; Configure whether a warning is emitted when a routine is called by another with JSR without
; being listed as a dependency 
; ::CALL65_SAFE_JSR = 1

; Scratch space memory segment
.import		__SCRATCH_START__, __SCRATCH_SIZE__

; ================================================================================================
; Default behaviours
.ifndef	::CALL65_WARNING_LEVEL
	::CALL65_WARNING_LEVEL = 2
.endif

.ifndef ::CALL65_SAFE_JSR
	::CALL65_SAFE_JSR = 1
.endif

; ================================================================================================
; Declare an exportable routine
;	name: name of routine and subsequent scope containing public local variables
.macro	.routine name
	; Declare label and anonymous scope for routine
	name:
	.scope ;

	; Define the routine name used for scoping local variables, initialize local stack, and
	; initialize dependency list
	.define	__ROUTINE_NAME__ name
	.define	__LOCAL_STACK__ __SCRATCH_START__ & $FF
	.define __DEPENDENCIES__
.endmac

; ================================================================================================
; End an exportable routine
.macro	.endroutine
	; Commit final stack size for exporting, and clean up defines for next routine
	::.ident(.sprintf("%s__LOCAL_STACK_SIZE__", .string(__ROUTINE_NAME__))) = __LOCAL_STACK__ 
	.undefine	__ROUTINE_NAME__
	.undefine	__LOCAL_STACK__
	.undefine	__DEPENDENCIES__

	; Close routine's anonymous scope
	.endscope
.endmac

; ================================================================================================
; Globalize a routine and it's associated public locals
;	name: name of routine and local variable scope to globalize
;	public_locals: space separated list of local variables to expose publicly
.macro	.globalroutine name, public_locals
	; Check whether routine is doubly defined
	.ifdef .ident(.sprintf("%s__EXISTS__", .string(name)))
		.error .sprintf("\x1B[1;31m%s is doubly defined!\x1B[0m", .string(name))
		.fatal ""
	.endif

	; Globalize routine address
	.global	name

	; Forward declare named scope for public locals
	.scope name
	.endscope

	; Globalize mangled local symbols, add un-mangled symbols to named scope
	__parse_public_locals name, {public_locals}

	; Globalize local stack size
	.globalzp	.ident(.sprintf("%s__LOCAL_STACK_SIZE__", .string(name)))

	; Define a unique symbol to facilitate dependency error checking
	::.ident(.sprintf("%s__EXISTS__", .string(name))) = 1
.endmac

; ================================================================================================
; Forward declare a routine to be used in the same translation unit
;	names: space separated list of routine local variable scopes to forward declare
.macro	.declareroutine names
	; Recursive base case
	.ifblank names
		.exitmac
	.endif

	; Check whether routine is doubly defined
	.ifdef .ident(.sprintf("%s__EXISTS__", .string(.left(1, {names}))))
		.error .sprintf("\x1B[1;31m%s is doubly defined!\x1B[0m", .string(.left(1, {names})))
		.fatal ""
	.endif

	; Forward declare named scope for public locals
	.scope .left(1, {names})
	.endscope

	; Define a unique symbol to facilitate dependency error checking
	::.ident(.sprintf("%s__EXISTS__", .string(.left(1, {names})))) = 1

	; Recur
	.declareroutine .right(.tcount({names}) - 1, {names})
.endmac

; ================================================================================================
; Indicate that a routine calls another, allocating enough scratch space to avoid collisions.
; Specifically, the scratch space index is set to the maximum of the current index and the scratch
; usage of all listed dependencies
;	dependencies: space separated list of routines
.macro	.calls dependencies
	; Verify that we're within a routine's scope
	.if .xmatch(__ROUTINE_NAME__, .ident("__ROUTINE_NAME__"))
		.error "\x1B[1;31m.calls statements are only valid between .routine and .endroutine!\x1B[0m"
		.fatal ""
	.endif

	; Warn if local stack is non-empty before declaring dependencies
	.if ::CALL65_WARNING_LEVEL > 1
		.assert	__LOCAL_STACK__ = 0, warning, "\x1B[1;35m.calls statements should be unified and come before local variable allocation!\x1B[0m"
	.endif

	__parse_dependencies dependencies
.endmac

; ================================================================================================
; Allocate space for a new local variable in scratch space. Note that such allocations should
; generally be performed after declaring dependencies; however, it can occasionally be useful to
; permit certain local variables to overlap the scratch space of dependencies. For example, when
; it is known that said variables need not maintain their state across calls to said dependencies
;	name: name of variable
;	width: number of bytes to allocate, assumed to be one if unspecified
.macro	.allocatelocal name, width
	; Verify that we're within a routine's scope
	.if .xmatch(__ROUTINE_NAME__, .ident("__ROUTINE_NAME__"))
		.error "\x1B[1;31m.allocatelocal statements are only valid between .routine and .endroutine!\x1B[0m"
		.fatal ""
	.endif

	; Declare local copy of the variable location, along with globally scoped mangled version for exporting
	name = __LOCAL_STACK__
	::.ident(.sprintf("__%s_%s", .string(__ROUTINE_NAME__), .string(name))) = name

	; Add un-mangled version to routine scope if it doesn't exist already
	.ifndef ::__ROUTINE_NAME__::name
		::__ROUTINE_NAME__::name = name
	.endif

	; Increment local stack
	.define __TEMP__ __LOCAL_STACK__
	.undefine __LOCAL_STACK__
	.ifnblank width
		.define __LOCAL_STACK__ __TEMP__ + width
	.else
		.define __LOCAL_STACK__ __TEMP__ + 1
	.endif
	.undefine __TEMP__

	; Verify that we haven't overflowed the local scratch space
	.assert __LOCAL_STACK__ <= __SCRATCH_START__ + __SCRATCH_SIZE__, error, .sprintf("\x1B[1;31mOverflowed local variable scratch space in %s!\x1B[0m", .string(__ROUTINE_NAME__))
.endmac

; ================================================================================================
; Recursive helper macro for .globalroutine
;	name: name of enclosing routine
;	public_locals: space separated list of local variables to expose publicly
.macro	__parse_public_locals name, public_locals
	; Recursive base case
	.ifblank public_locals
		.exitmac
	.endif

	; Globalize mangled local symbol as zeropage
	.globalzp .ident(.sprintf("__%s_%s", .string(name), .string(.left(1, {public_locals}))))

	; Add un-mangled local symbol to routine scope
	name::.left(1, {public_locals}) = .ident(.sprintf("__%s_%s", .string(name), .string(.left(1, {public_locals}))))

	; The ol' parse-a-roo
	__parse_public_locals name, .right(.tcount({public_locals}) - 1, {public_locals})
.endmac

; ================================================================================================
; Recursive helper macro for .calls
;	dependencies: space separated list of routines
.macro	__parse_dependencies dependencies
	; Recursive base case
	.ifblank dependencies
		.exitmac
	.endif

	; Check whether dependency is defined in the current context
	.ifndef .ident(.sprintf("%s__EXISTS__", .string(.left(1, {dependencies}))))
		.error .sprintf("\x1B[1;31mCould not find %s in current context!\x1B[0m Maybe you forgot to include it?", .string(.left(1, {dependencies})))
		.fatal ""
	.endif

	; Leave enough space for the union of all dependencies' locals
	.define __TEMP__ .max(__LOCAL_STACK__, .ident(.sprintf("%s__LOCAL_STACK_SIZE__", .string(.left(1, {dependencies})))))
	.undefine __LOCAL_STACK__
	.define __LOCAL_STACK__ __TEMP__
	.undefine __TEMP__

	; Add dependency to dependency list
	.define __TEMP__ __DEPENDENCIES__
	.undefine __DEPENDENCIES__
	.define __DEPENDENCIES__ __TEMP__ .left(1, {dependencies})
	.undefine __TEMP__

	; It's parsing all the way down
	__parse_dependencies .right(.tcount({dependencies}) - 1, {dependencies})
.endmac

; Only provide JSR overload if requested
.if ::CALL65_SAFE_JSR <> 0
	.feature	ubiquitous_idents +

	; ================================================================================================
	; Version of JSR with error checking. Emits a warning if target is not present in dependency list
	; when called within the scope of a routine
	;	target: target routine to be called
	.macro	jsr target
		; Emit requested code
		.byte $20, <(target), >(target)

		; Don't perform dependency check if we're outside of a routine scope
		.if .xmatch(__ROUTINE_NAME__, .ident("__ROUTINE_NAME__"))
			.exitmac
		.endif

		; Also don't perform dependency check if target is not declared as a routine
		.ifndef .ident(.sprintf("%s__EXISTS__", .string(target)))
			.exitmac
		.endif

		; Check for target within dependency list if warning level is sufficient
		.if ::CALL65_WARNING_LEVEL > 0
			__check_dependencies target, __DEPENDENCIES__
		.endif
	.endmac

	; ================================================================================================
	; Recursive helper macro for safe JSR
	;	target: symbol to check for
	;	dependencies: list of dependencies to check against
	.macro	__check_dependencies target, dependencies
		; Recursive base cases
		.ifblank dependencies
			; If we reach this then the target is not in the dependency list
			.warning .sprintf("\x1B[1;35mCould not find %s in dependency list for %s\x1B[0m", .string(target), .string(__ROUTINE_NAME__))
			.exitmac
		.elseif .xmatch(.left(1, {dependencies}), target)
			; If we reach this, then it was
			.exitmac
		.endif

		; Hold my macro, I'm going in!
		__check_dependencies target, .right(.tcount({dependencies}) - 1, {dependencies})
	.endmac

	; ================================================================================================
	; Uppercase aliases for above macros.
	.macro	JSR target
		jsr target
	.endmac
.endif

.feature	string_escapes -
.endif