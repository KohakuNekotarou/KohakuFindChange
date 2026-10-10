//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC), copied from KIDMCP (written for Kohaku Change Marker) - the object rows' fingerprint (1.4.0)
//
//  An IXferBytes that keeps everything in memory, so a snippet can be exported without a file
//  ever existing.
//
//  StreamUtil::CreateMemoryStreamWrite takes an IXferBytes, and the SDK ships no usable
//  implementation of one: public/libs/publiclib/strings/WideString.cpp:37 includes
//  "MemXferBytes.h", but that header appears nowhere in the SDK, so the class it names is
//  product-internal and cannot be reached from plug-in code. The one public implementation is
//  hostadapter/IDHAMemoryXferBytes. This is that idea rewritten over std::vector so the buffer
//  grows by itself, and with the written bytes handed out directly - reading them back through
//  a second stream, the way the sample does, buys nothing when the caller wants the bytes.
//
//========================================================================================

#ifndef __KFCMemXferBytes_h__
#define __KFCMemXferBytes_h__

#include "IXferBytes.h"

#include <vector>

/** A memory-backed IXferBytes for exporting to a stream that never touches disk.
	@ingroup KFC
*/
class KFCMemXferBytes : public IXferBytes
{
public:
	KFCMemXferBytes();
	virtual ~KFCMemXferBytes();

	/** See IXferBytes::Read. Reads what is left from the current position; a request for more
		than that transfers what there is and leaves the stream at end-of-stream.
	*/
	virtual uint32 Read(void* buffer, uint32 num);

	/** See IXferBytes::Write. Grows the buffer as needed, so a caller never has to size it; one that cannot grow fails
		the write (0 transferred, the stream failed) - no exception leaves it. */
	virtual uint32 Write(void* buffer, uint32 num);

	/** See IXferBytes::Seek. Seeking past the end is allowed and does not grow the buffer -
		the next Write is what grows it, which is the behaviour a stream expects.
	*/
	virtual uint64 Seek(int64 numberOfBytes, SeekFromWhere fromHere);

	/** See IXferBytes::Flush. Nothing is buffered elsewhere, so this does nothing. */
	virtual void Flush();

	/** See IXferBytes::GetStreamState. */
	virtual StreamState GetStreamState();

	/** See IXferBytes::SetEndOfStream. Truncates to the current position. */
	virtual void SetEndOfStream();

	/** The bytes written so far. Not null-terminated - always pair it with GetSize().
		@return pointer to the first byte, or nil when nothing has been written.
	*/
	const char* GetData() const;

	/** How many bytes GetData points at. */
	uint32 GetSize() const;

private:
	std::vector<char> fBuffer;
	uint32 fPosition;
	StreamState fStreamState;
};

#endif // __KFCMemXferBytes_h__

// End, KFCMemXferBytes.h.
