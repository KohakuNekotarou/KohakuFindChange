//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  KBSGlyphView: draws ONE glyph in the font that defines it, for the Glyph tab's replace
//  confirmation. Built like KBSColorTextView - a DVControlView whose Draw() paints through an
//  AGMGraphicsContext - but it draws a GLYPH ID rather than a string, which is the whole point:
//  xshow() takes glyph ids directly, so an alternate form comes out as itself instead of as the
//  standard form its Unicode would map back to.
//
//  Which of the two glyphs it draws is decided by its own WidgetID (see
//  KBSGlyphConfirmDialog::GetSideForWidget) - the prompt has exactly two frames, so a data
//  interface of its own would be one indirection for nothing.
//
//  An EMPTY frame is a valid state: an empty Change To box means "delete every match", and a
//  deletion has no glyph to show. The frame is still drawn, because an empty box after the arrow
//  is what "nothing goes in here" looks like.
//
//  The class itself is private to the .cpp; this header exists so the file pair matches the rest
//  of the project and can be registered in the .vcxproj alongside its source.
//
//========================================================================================

#ifndef __KBSGlyphView_h__
#define __KBSGlyphView_h__

// Nothing to declare: KBSGlyphView is created by the framework from the boss definition in KBS.fr
// (kKBSGlyphViewWidgetBoss / kKBSGlyphViewImpl) and is never referred to by name from another
// translation unit.

#endif // __KBSGlyphView_h__

// End, KBSGlyphView.h.
