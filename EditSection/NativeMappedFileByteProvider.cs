﻿//  Copyright 2015 Google Inc. All Rights Reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//  http ://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

using Be.Windows.Forms;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace EditSection
{
    class NativeMappedFileByteProvider : IByteProvider
    {
        NativeMappedFile _map;
        bool _readOnly;

        public NativeMappedFileByteProvider(NativeMappedFile map, bool readOnly)
        {
            _readOnly = readOnly;
            _map = map;
        }

        public void ApplyChanges()
        {
            System.Diagnostics.Trace.WriteLine("In ApplyChanges");
        }
        
        public event EventHandler Changed;

        public void DeleteBytes(long index, long length)
        {
            throw new NotImplementedException();
        }

        public bool HasChanges()
        {
            return false;
        }

        public void InsertBytes(long index, byte[] bs)
        {            
        }

        public long Length
        {
            get { return (long)_map.GetSize(); }
        }

        public event EventHandler LengthChanged;

        public byte ReadByte(long index)
        {
            if (index < _map.GetSize())
            {
                return Marshal.ReadByte(_map.DangerousGetHandle(), (int)index);
            }

            return 0;
        }

        public bool SupportsDeleteBytes()
        {
            return false;
        }

        public bool SupportsInsertBytes()
        {
            return false;
        }

        public bool SupportsWriteByte()
        {
            return !_readOnly;
        }

        public void WriteByte(long index, byte value)
        {
            if (index < _map.GetSize())
            {
                try
                {
                    Marshal.WriteByte(_map.DangerousGetHandle(), (int)index, value);
                }
                catch
                {
                }
            }
        }
    }
}
