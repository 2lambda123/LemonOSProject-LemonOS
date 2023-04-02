#include <CharacterBuffer.h>

#include <CString.h>
#include <Lock.h>
#include <Logging.h>
#include <MM/KMalloc.h>

CharacterBuffer::CharacterBuffer() {
    m_buffer = new char[bufferSize];
    bufferPos = 0;
    lock = 0;
    lines = 0;
}

CharacterBuffer::~CharacterBuffer() {
    if(m_buffer){
        delete m_buffer;
    }
}

ssize_t CharacterBuffer::Write(char* _buffer, size_t size) {
    if (bufferPos + size > maxBufferSize) {
        size = maxBufferSize - bufferPos;
    }

    if (size == 0) {
        return 0;
    }

    ScopedSpinLock lock{this->lock};

    if ((bufferPos + size) > bufferSize) {
        char* oldBuf = m_buffer;
        m_buffer = (char*)kmalloc(bufferPos + size + 128);
        memcpy(m_buffer, oldBuf, bufferSize);
        bufferSize = bufferPos + size + 128;

        if(oldBuf) {
            kfree(oldBuf);
        }
    }

    ssize_t written = 0;

    for (unsigned i = 0; i < size; i++) {
        if (_buffer[i] == '\b' /*Backspace*/ && !ignoreBackspace) {
            if (bufferPos > 0) {
                bufferPos--;
                written++;
            }
            continue;
        } else {
            m_buffer[bufferPos++] = _buffer[i];
            written++;
        }

        if (_buffer[i] == '\n' || _buffer[i] == '\0')
            lines++;
    }

    return written;
}

ssize_t CharacterBuffer::Read(char* _buffer, size_t count) {
    ScopedSpinLock lock{this->lock};
    if (count <= 0) {
        return 0;
    }

    int i = 0;
    int readPos = 0;
    for (; readPos < bufferPos && i < count; readPos++) {
        if (m_buffer[readPos] == '\0') {
            lines--;
            continue;
        }

        _buffer[i] = m_buffer[readPos];
        i++;

        if (m_buffer[readPos] == '\n')
            lines--;
    }

    for (unsigned j = 0; j < bufferSize - readPos; j++) {
        m_buffer[j] = m_buffer[readPos + j];
    }

    bufferPos -= readPos;

    return i;
}

ErrorOr<ssize_t> CharacterBuffer::Write(UIOBuffer* buffer, size_t size) {
    char _buf[512];
    size_t bytesLeft = size;

    while(bytesLeft > 512) {
        if(buffer->Read((uint8_t*)_buf, 512)) {
            return EFAULT;
        }

        int r = Write(_buf, 512);
        bytesLeft -= r;
        
        // Buffer might be full, not all of our data was written
        if(r < 512) {
            return size - bytesLeft;
        }
    }

    if(buffer->Read((uint8_t*)_buf, bytesLeft)) {
        return EFAULT;
    }

    bytesLeft -= Write(_buf, bytesLeft);
    return size - bytesLeft;
}

ErrorOr<ssize_t> CharacterBuffer::Read(UIOBuffer* buffer, size_t size) {
    char _buf[512];
    size_t bytesLeft = size;

    while(bytesLeft > 512) {
        size_t r = Read(_buf, 512);
        bytesLeft -= r;

        if(buffer->Write((uint8_t*)_buf, r)) {
            return EFAULT;
        }

        // Buffer might be empty, bytes read was under count
        if(r < 512) {
            return size - bytesLeft;
        }
    }

    int r = Read(_buf, bytesLeft);
    if(buffer->Write((uint8_t*)_buf, r)) {
        return EFAULT;
    }

    return size - (bytesLeft - r);
}

void CharacterBuffer::Flush() {
    ScopedSpinLock lock{this->lock};

    bufferPos = 0;
    lines = 0;
}