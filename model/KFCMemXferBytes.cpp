//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC), copied from KIDMCP (written for Kohaku Change Marker) - the object rows' fingerprint (1.4.0)
//
//  See KFCMemXferBytes.h for why this exists rather than the SDK's own MemXferBytes.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include <cstring>
#include <new>				// std::bad_alloc - a buffer that cannot grow (Write)

// Project includes:
#include "KFCMemXferBytes.h"

/* Constructor
*/
KFCMemXferBytes::KFCMemXferBytes()
	: fPosition(0),
	  fStreamState(kStreamStateGood)
{
}

/* Destructor
*/
KFCMemXferBytes::~KFCMemXferBytes()
{
}

/* Read
*/
uint32 KFCMemXferBytes::Read(void* buffer, uint32 num)
{
	if (buffer == nil || num == 0)
		return 0;

	const uint32 stored = static_cast<uint32>(fBuffer.size());
	if (fPosition >= stored)
	{
		fStreamState = kStreamStateEOF;
		return 0;
	}

	uint32 available = stored - fPosition;
	uint32 toTransfer = num;
	if (toTransfer > available)
	{
		toTransfer = available;
		fStreamState = kStreamStateEOF;
	}

	std::memcpy(buffer, &fBuffer[fPosition], toTransfer);
	fPosition += toTransfer;
	return toTransfer;
}

/* Write
   Seeking past the end and then writing is legal for a stream, so the gap is zero-filled
   rather than refused - resize() does that for us.
   NO EXCEPTION LEAVES HERE: this is called from inside InDesign's exporter, and an exception
   crossing InDesign's code takes the application down (memory k2-scoped-ptr-and-array). A buffer
   that cannot grow is a failed write - nothing transferred, the stream failed - and the export
   that asked for it fails with it (KFCObjectSearch::Fingerprint then answers "no fingerprint").
*/
uint32 KFCMemXferBytes::Write(void* buffer, uint32 num)
{
	if (buffer == nil || num == 0)
		return 0;

	const uint32 end = fPosition + num;
	if (end > fBuffer.size())
	{
		try
		{
			fBuffer.resize(end, 0);
		}
		catch (const std::bad_alloc&)
		{
			fStreamState = kStreamStateFailure;
			return 0;
		}
	}

	std::memcpy(&fBuffer[fPosition], buffer, num);
	fPosition = end;
	return num;
}

/* Seek
*/
uint64 KFCMemXferBytes::Seek(int64 numberOfBytes, SeekFromWhere fromHere)
{
	if (fStreamState == kStreamStateEOF)
		fStreamState = kStreamStateGood;

	int64 target = 0;
	switch (fromHere)
	{
	case kSeekFromStart:
		target = numberOfBytes;
		break;
	case kSeekFromCurrent:
		target = static_cast<int64>(fPosition) + numberOfBytes;
		break;
	case kSeekFromEnd:
		target = static_cast<int64>(fBuffer.size()) + numberOfBytes;
		break;
	}

	// A negative position is not representable; clamping to the start is what the caller can
	// still work with, and it keeps the memcpy above in range whatever it was asked for.
	if (target < 0)
		target = 0;

	fPosition = static_cast<uint32>(target);
	return fPosition;
}

/* Flush
*/
void KFCMemXferBytes::Flush()
{
}

/* GetStreamState
*/
StreamState KFCMemXferBytes::GetStreamState()
{
	return fStreamState;
}

/* SetEndOfStream
*/
void KFCMemXferBytes::SetEndOfStream()
{
	if (fPosition < fBuffer.size())
		fBuffer.resize(fPosition);
}

/* GetData
*/
const char* KFCMemXferBytes::GetData() const
{
	if (fBuffer.empty())
		return nil;
	return &fBuffer[0];
}

/* GetSize
*/
uint32 KFCMemXferBytes::GetSize() const
{
	return static_cast<uint32>(fBuffer.size());
}

// End, KFCMemXferBytes.cpp.
