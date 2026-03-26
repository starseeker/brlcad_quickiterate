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

#ifndef GEOBRLCAD_BASIC_STRING
#define GEOBRLCAD_BASIC_STRING

#include <geogram/basic/geogram_common.h>
#include <geogram/basic/numeric.h>

#include <string>
#include <sstream>
#include <stdexcept>
#include <iomanip>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

/**
 * \file geogram/basic/string.h
 * \brief Functions for string manipulation
 */

namespace GEOBRL {

    /*
     * \brief String manipulation utilities.
     */
    namespace String {

        /**
         * \brief Creates a string from a format string and additional
         *  arguments. Works like sprintf()
         * \param[in] format the format string
         */
        std::string GEOBRLCAD_API format(const char* format, ...)
#ifndef GOMGEN
#ifdef GEOBRL_COMPILER_GCC_FAMILY
        // Tells the compiler that format is a printf-like format
        // string, so that it can check that the arguments match
        // the format string and bark at you if it is not the case.
            __attribute__ ((__format__(printf, 1, 2)))
#endif
#endif
            ;

        /**
         * \brief Converts a typed value to a string
         * \param[in] value the typed value to convert
         * \return a string that contain the stringified form of the value
         */
        template <class T>
        inline std::string to_string(const T& value) {
            std::ostringstream out;
            // Makes sure that double-precision number are displayed
            // with a sufficient number of digits. This is important
            // to avoid losing precision when using ASCII files.
            out << std::setprecision(17);
            out << value;
            return out.str();
        }

        /**
         * \brief Converts a boolean value to a string
         * \param[in] value the boolean value to convert
         * \return string \c "true" if the boolean value is true
         * or \c "false" if the boolean value is false
         */
        template <>
        inline std::string to_string(const bool& value) {
            return value ? "true" : "false";
        }

        /**
         * \brief Conversion exception
         * \details This exception is thrown by the conversion functions
         * to_bool(), to_int() and to_double() when a string cannot be
         * converted to the desired type.
         */
        class GEOBRLCAD_API ConversionError : public std::logic_error {
        public:
            /**
             * \brief Constructs a conversion exception
             * \param[in] s the input string that could not be converted
             * \param[in] type the expected destination type
             */
            ConversionError(const std::string& s, const std::string& type);

            /**
             * \brief Gets the string identifying the exception
             */
            const char* what() const GEOBRL_NOEXCEPT override;
        };

        /**
         * \brief Converts a C string to a typed value
         * \details This is a generic version that uses a std::istringstream
         * to extract the value from the string. This function is specialized
         * for integral types to reach the maximum efficiency.
         * \param[in] s the source string
         * \param[out] value the typed value
         * \retval true if the conversion was successful
         * \retval false otherwise
         */
        template <class T>
        inline bool from_string(const char* s, T& value) {
            std::istringstream in(s);
            return (in >> value) && (in.eof() || ((in >> std::ws) && in.eof()));
        }

        /**
         * \brief Converts a std::string to a typed value
         * \details This is a generic version that uses a std::istringstream
         * to extract the value from the string. This function is specialized
         * for integral types to reach the maximum efficiency.
         * \param[in] s the source string
         * \param[out] value the typed value
         * \retval true if the conversion was successful
         * \retval false otherwise
         */
        template <class T>
        inline bool from_string(const std::string& s, T& value) {
            return from_string(s.c_str(), value);
        }

        /**
         * \brief Converts a string to a double value
         * \param[in] s the source string
         * \param[out] value the double value
         * \retval true if the conversion was successful
         * \retval false otherwise
         */
        template <>
        inline bool from_string(const char* s, double& value) {
            errno = 0;
            char* end;
            value = strtod(s, &end);
            return end != s && *end == '\0' && errno == 0;
        }

        /**
         * \brief Converts a string to a signed integer value
         * \param[in] s the source string
         * \param[out] value the integer value
         * \retval true if the conversion was successful
         * \retval false otherwise
         */
        template <typename T>
        inline bool string_to_signed_integer(const char* s, T& value) {
            errno = 0;
            char* end;
#ifdef GEOBRL_OS_WINDOWS
            Numeric::int64 v = _strtoi64(s, &end, 10);
#else
            Numeric::int64 v = strtoll(s, &end, 10);
#endif
            if(
                end != s && *end == '\0' && errno == 0 &&
                v >= std::numeric_limits<T>::min() &&
                v <= std::numeric_limits<T>::max()
            ) {
                value = static_cast<T>(v);
                return true;
            }

            return false;
        }

        /**
         * \brief Converts a string to a Numeric::int8 value
         * \see string_to_signed_integer()
         */
        template <>
        inline bool from_string(const char* s, Numeric::int8& value) {
            return string_to_signed_integer(s, value);
        }

        /**
         * \brief Converts a string to a Numeric::int16 value
         * \see string_to_signed_integer()
         */
        template <>
        inline bool from_string(const char* s, Numeric::int16& value) {
            return string_to_signed_integer(s, value);
        }

        /**
         * \brief Converts a string to a Numeric::int32 value
         * \see string_to_signed_integer()
         */
        template <>
        inline bool from_string(const char* s, Numeric::int32& value) {
            return string_to_signed_integer(s, value);
        }

        /**
         * \brief Converts a string to a Numeric::int64 value
         */
        template <>
        inline bool from_string(const char* s, Numeric::int64& value) {
            errno = 0;
            char* end;
#ifdef GEOBRL_OS_WINDOWS
            value = _strtoi64(s, &end, 10);
#else
            value = strtoll(s, &end, 10);
#endif
            return end != s && *end == '\0' && errno == 0;
        }

        /**
         * \brief Converts a string to a unsigned integer value
         * \param[in] s the source string
         * \param[out] value the integer value
         * \retval true if the conversion was successful
         * \retval false otherwise
         */
        template <typename T>
        inline bool string_to_unsigned_integer(const char* s, T& value) {
            errno = 0;
            char* end;
#ifdef GEOBRL_OS_WINDOWS
            Numeric::uint64 v = _strtoui64(s, &end, 10);
#else
            Numeric::uint64 v = strtoull(s, &end, 10);
#endif
            if(
                end != s && *end == '\0' && errno == 0 &&
                v <= std::numeric_limits<T>::max()
            ) {
                value = static_cast<T>(v);
                return true;
            }

            return false;
        }

        /**
         * \brief Converts a string to a Numeric::uint8 value
         * \see string_to_unsigned_integer()
         */
        template <>
        inline bool from_string(const char* s, Numeric::uint8& value) {
            return string_to_unsigned_integer(s, value);
        }

        /**
         * \brief Converts a string to a Numeric::uint16 value
         * \see string_to_unsigned_integer()
         */
        template <>
        inline bool from_string(const char* s, Numeric::uint16& value) {
            return string_to_unsigned_integer(s, value);
        }

        /**
         * \brief Converts a string to a Numeric::uint32 value
         * \see string_to_unsigned_integer()
         */
        template <>
        inline bool from_string(const char* s, Numeric::uint32& value) {
            return string_to_unsigned_integer(s, value);
        }

        /**
         * \brief Converts a string to a Numeric::uint64 value
         */
        template <>
        inline bool from_string(const char* s, Numeric::uint64& value) {
            errno = 0;
            char* end;
#ifdef GEOBRL_OS_WINDOWS
            value = _strtoui64(s, &end, 10);
#else
            value = strtoull(s, &end, 10);
#endif
            return end != s && *end == '\0' && errno == 0;
        }

        /**
         * \brief Converts a string to a boolean value
         * \details
         * Legal values for the true boolean value are "true","True" and "1".
         * Legal values for the false boolean value are "false","False" and "0".
         * \param[in] s the source string
         * \param[out] value the boolean value
         * \retval true if the conversion was successful
         * \retval false otherwise
         */
        template <>
        inline bool from_string(const char* s, bool& value) {
            if(strcmp(s, "true") == 0 ||
               strcmp(s, "True") == 0 ||
               strcmp(s, "1") == 0
              ) {
                value = true;
                return true;
            }
            if(strcmp(s, "false") == 0 ||
               strcmp(s, "False") == 0 ||
               strcmp(s, "0") == 0
              ) {
                value = false;
                return true;
            }
            return false;
        }

        /**
         * \brief Converts a string to an int
         * \details If the entire string cannot be
         * converted to an int, the function
         * throws an exception ConversionError.
         * \param[in] s the source string
         * \return the extracted integer value
         * \see ConversionError
         */
        inline int to_int(const std::string& s) {
            int value;
            if(!from_string(s, value)) {
                throw ConversionError(s, "integer");
            }
            return value;
        }

        /**
         * \brief Converts a string to an unsigned int
         * \details If the entire string cannot be
         * converted to an unsigned int, the function
         * throws an exception ConversionError.
         * \param[in] s the source string
         * \return the extracted integer value
         * \see ConversionError
         */
        inline unsigned int to_uint(const std::string& s) {
            unsigned int value;
            if(!from_string(s, value)) {
                throw ConversionError(s, "integer");
            }
            return value;
        }

        /**
         * \brief Converts a string to a double
         * \details If the entire string cannot be
         * converted to a double, the function
         * throws an exception ConversionError.
         * \param[in] s the source string
         * \return the extracted double value
         * \see ConversionError
         */
        inline double to_double(const std::string& s) {
            double value;
            if(!from_string(s, value)) {
                throw ConversionError(s, "double");
            }
            return value;
        }

        /**
         * \brief Converts a string to a boolean
         * \details If the entire string cannot be
         * converted to a boolean, the function
         * throws an exception ConversionError.
         * \param[in] s the source string
         * \return the extracted boolean value
         * \see ConversionError
         */
        inline bool to_bool(const std::string& s) {
            bool value;
            if(!from_string(s, value)) {
                throw ConversionError(s, "boolean");
            }
            return value;
        }

    }
}

#endif
