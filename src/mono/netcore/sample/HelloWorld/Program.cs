using System;
using System.Reflection;
using System.Runtime.Loader;
using System.Runtime.CompilerServices;

namespace HelloWorld
{
    class CollectibleALC : AssemblyLoadContext {
        public CollectibleALC() : base(isCollectible: true) {}
        protected override Assembly Load(AssemblyName assemblyName) {
            return this.LoadFromAssemblyPath("/Users/ryan/git/runtime/src/mono/netcore/sample/HelloWorld/bin/HelloWorld.dll");
        }
    }
    class SampleType {
        public string name = "test";
    }
    class Program
    {
        private static Type t;

        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void Execute(int i) {
            var calc = new CollectibleALC();
            {
                var asm = calc.LoadFromAssemblyName(new AssemblyName("HelloWorld"));
                var type = asm.GetType("HelloWorld.Program");
                var method = type.GetMethod("Hello");
                //var instance = Activator.CreateInstance(type);
                method.Invoke(null, new object[] { i });
            }
            calc.Unload();

        }

        public static void Hello(int i) {
            Console.WriteLine($"Hello {i}!");
        }

        static void Main(string[] args)
        {
            for (var i = 0; i < 10; i++) {
                Execute(i);
            }
            GC.Collect();
            GC.WaitForPendingFinalizers();
            Console.ReadKey();

            var test_alc = new CollectibleALC();
            {
                var asm = test_alc.LoadFromAssemblyName(new AssemblyName("HelloWorld"));
                var type = asm.GetType("HelloWorld.Program");
                t = type;
            }
            test_alc.Unload();
            GC.Collect();
            GC.WaitForPendingFinalizers();
            Console.ReadKey();

            t = null;
            GC.Collect();
            GC.WaitForPendingFinalizers();
            Console.ReadKey();
        }
    }
}
