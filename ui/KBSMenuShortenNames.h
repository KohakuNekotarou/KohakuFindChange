//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS) - "Shorten Main Menu Names": the Japanese it shows. The feature exists only in
//  a Japanese UI (the user's call, 2026-10-03), so there is no English twin.
//
//  ***** THIS FILE IS UTF-8 WITH BOM ***** (the rule of model/KBSLoc.h): the L"..." literals stay readable.
//
//========================================================================================

#pragma once
#ifndef __KBSMenuShortenNames_h__
#define __KBSMenuShortenNames_h__

namespace KBSMenuShortenNames
{
	// The six columns' short titles. The (&X) keeps InDesign's own Alt letter for each column.
	const wchar_t* const kFile    = L"記録(&F)";
	const wchar_t* const kLayout  = L"割付(&L)";
	const wchar_t* const kObject  = L"図形(&O)";
	const wchar_t* const kPlugins = L"拡張(&P)";
	const wchar_t* const kWindow  = L"窓(&W)";
	const wchar_t* const kHelp    = L"助(&H)";

	// The panel menu item, and what the status line says.
	const wchar_t* const kMenuItem       = L"メインメニュー名を短縮";
	const wchar_t* const kStatusOn       = L"メインメニュー名を短縮: オン";
	const wchar_t* const kStatusOff      = L"メインメニュー名を短縮: オフ";
	const wchar_t* const kStatusNoRecord = L"メインメニュー名を短縮: メニューの記録がありません（KFC を入れ直した後の最初の起動で取られます）";
	const wchar_t* const kStatusNotSaved = L"（設定ファイルに書けませんでした）";
}

#endif // __KBSMenuShortenNames_h__
