// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Runtime.Intrinsics;
using System.Runtime.Intrinsics.Arm;

namespace HelloWorld
{
    internal class Program
    {
        private static void Main(string[] args)
        {
            Console.WriteLine(Dp.IsSupported);
            if (Dp.IsSupported) {
                Vector128<UInt32> a = Vector128<UInt32>.Zero;
                Vector128<Byte> b = Vector128<Byte>.Zero;
                Vector128<Byte> c = Vector128<Byte>.Zero;
                Vector128<UInt32> d = Dp.DotProduct(a, b, c);
                Console.WriteLine(d);
            } else {
                Console.WriteLine("sad");
            }
        }
    }
}
