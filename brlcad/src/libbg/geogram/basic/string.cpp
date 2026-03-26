/*
 *  Copyright (c) 2000-2022 Inria
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *  * Neither the name of the ALICE Project-Team nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  Contact: Bruno Levy
 *
 *     https://www.inria.fr/fr/bruno-levy
 *
 *     Inria,
 *     Domaine de Voluceau,
 *     78150 Le Chesnay - Rocquencourt
 *     FRANCE
 *
 */

#include <geogram/basic/string.h>
#include <stdarg.h>

namespace GEOBRL {

    /**
     * \brief Builds the conversion error message
     * \param[in] s the input string that could not be converted
     * \param[in] type the expected destination type
     * \return a string that contains the error message
     */
    static std::string conversion_error(
        const std::string& s, const std::string& type
    ) {
        std::ostringstream out;
        out << "Conversion error: cannot convert string '"
            << s << "' to " << type;
        return out.str();
    }
}

namespace GEOBRL {

    namespace String {

        std::string format(const char* format, ...) {
            size_t length = 0;

            // Determine required length
            va_list arg_ptr;
            va_start(arg_ptr, format);
            length = size_t(vsnprintf(nullptr, 0, format, arg_ptr));
            va_end(arg_ptr);

            // Create the string of required length and sprintf() into it
            std::string result(length,'*');
            va_start(arg_ptr, format);
            vsnprintf(
		const_cast<char*>(result.c_str()), length+1, format, arg_ptr
	    );
            va_end(arg_ptr);

            return result;
        }

        /********************************************************************/

        ConversionError::ConversionError(
            const std::string& s, const std::string& type
        ) :
            std::logic_error(conversion_error(s, type)) {
        }

        const char* ConversionError::what() const GEOBRL_NOEXCEPT {
            return std::logic_error::what();
        }
    }
}
