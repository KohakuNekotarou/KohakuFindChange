//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Four-character ScriptIDs (OSTypes) for this plug-in's scripting additions.
//
//  These are what a script engine actually matches on at run time - the C++ enum names below only
//  separate them for the compiler. Two plug-ins that pick the same four characters are
//  indistinguishable to the engine, which is why they are allocated centrally:
//
//      docs/ai-notes/kes-scriptid-registry.md
//
//  The scheme is [kind][K][plug-in letter][member]:
//      kind   p = property (and method arguments), e = method, n = enumerator
//      K      fixed - the author's signature, the same in every one of these plug-ins
//      letter this plug-in's own letter; B = KBS. A different letter cannot collide.
//      member one character, unique within this plug-in and kind
//
//  Checked against the application's own list (source/public/includes/ScriptingDefs.h) and
//  GenericID.h before being taken: no hits.
//
//========================================================================================

#ifndef __KBSScriptingDefs_h__
#define __KBSScriptingDefs_h__

/** Property ScriptIDs. */
enum KBSScriptProperties
{
	p_KBSStatus  = 'pKBs',	// p=property  K=Kohaku  B=KBS  s=status
	p_KBSResults = 'pKBr'	// p=property  K=Kohaku  B=KBS  r=results
};

#endif // __KBSScriptingDefs_h__

// End, KBSScriptingDefs.h.
