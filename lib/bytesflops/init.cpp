/*
 * Instrument code to keep track of run-time behavior:
 * LLVM pass initialization
 *
 * By Scott Pakin <pakin@lanl.gov>
 * and Pat McCormick <pat@lanl.gov>
 */

#include <iostream>
#include "bytesflops.h"
#include "FunctionKeyGen.h"
#include "BytesMP.h"

namespace bytesflops_pass {

    void BytesFlops::initializeKeyMap(Module& module)
    {
        if ( module.getFunction("func_key_map_ctor") )
            return;

        LLVMContext& ctx = module.getContext();

        // Type Definitions
        std::vector<Type*>StructTy_1_fields;
        StructTy_1_fields.push_back(IntegerType::get(ctx, 32));
        std::vector<Type*>FuncTy_3_args;
        FunctionType* FuncTy_3 = FunctionType::get(
         /*Result=*/Type::getVoidTy(ctx),
         /*Params=*/FuncTy_3_args,
         /*isVarArg=*/false);

        PointerType* PointerTy_2 = PointerType::get(FuncTy_3, 0);

        StructTy_1_fields.push_back(PointerTy_2);
        StructType *StructTy_1 = StructType::get(ctx, StructTy_1_fields, /*isPacked=*/false);

        ArrayType* ArrayTy_0 = NULL;

        // Function Declarations
        func_map_ctor = Function::Create(
                        /*Type=*/FuncTy_3,
                        /*Linkage=*/GlobalValue::InternalLinkage,
                        /*Name=*/"func_key_map_ctor", &module);
        func_map_ctor->setCallingConv(CallingConv::C);

        AttributeSet func__ZL4initv_PAL;
        SmallVector<AttributeSet, 4> Attrs;
        AttributeSet PAS;
        AttrBuilder B;
        B.addAttribute(Attribute::NoUnwind);
        B.addAttribute(Attribute::UWTable);
        PAS = AttributeSet::get(ctx, ~0U, B);

        Attrs.push_back(PAS);
        func__ZL4initv_PAL = AttributeSet::get(ctx, Attrs);
        func_map_ctor->setAttributes(func__ZL4initv_PAL);

        // Global Variable Declarations
        // Constant Definitions
        std::vector<Constant*> ctor_elems;
        std::vector<Constant*> const_struct_8_fields;
        ConstantInt* const_int32_9 = ConstantInt::get(ctx, APInt(32, StringRef("65535"), 10));
        const_struct_8_fields.push_back(const_int32_9);
        const_struct_8_fields.push_back(func_map_ctor);
        Constant* const_struct_8 = ConstantStruct::get(StructTy_1, const_struct_8_fields);
        ctor_elems.push_back(const_struct_8);

        GlobalVariable* llvm_global_ctors;

        GlobalVariable*
        current_ctors = module.getGlobalVariable("llvm.global_ctors");
        if ( !current_ctors )
        {
            ArrayTy_0 = ArrayType::get(StructTy_1, 1);
        }
        else
        {
            Constant * initializer = current_ctors->getInitializer();

            ConstantArray *
            ar = llvm::dyn_cast<ConstantArray>(initializer);
            assert( ar );

            int cnt = 0;
            Constant *
            elt = ar->getAggregateElement(cnt);
            while ( elt )
            {
                assert( llvm::dyn_cast<ConstantStruct>(elt) );
                ctor_elems.push_back(elt);
                cnt++;
                elt = ar->getAggregateElement(cnt);
            }
            ArrayTy_0 = ArrayType::get(StructTy_1, ctor_elems.size());
            current_ctors->eraseFromParent();

            // add our ctor to the llvm.global_ctors array
//            gvar_array_llvm_global_ctors->replaceAllUsesWith(new_llvm_ctors);
        }
        llvm_global_ctors = new GlobalVariable(/*Module=*/module,
                                                          /*Type=*/ArrayTy_0,
                                                          /*isConstant=*/false,
                                                          /*Linkage=*/GlobalValue::AppendingLinkage,
                                                          /*Initializer=*/0, // has initializer, specified below
                                                          /*Name=*/"llvm.global_ctors");


        Constant* ctor_array = ConstantArray::get(ArrayTy_0, ctor_elems);

        // Global Variable Definitions
        llvm_global_ctors->setInitializer(ctor_array);


    }


  // Initialize the BytesFlops pass.
  bool BytesFlops::doInitialization(Module& module) {
    // Inject external declarations to various variables defined in byfl.c.
    LLVMContext& globctx = module.getContext();
    IntegerType* i64type = Type::getInt64Ty(globctx);
    PointerType* i64ptrtype = Type::getInt64PtrTy(globctx);

    //initializeKeyMap(module);
      
    mem_insts_var      = declare_global_var(module, i64ptrtype, "bf_mem_insts_count", true);
    inst_mix_histo_var = declare_global_var(module, i64ptrtype, "bf_inst_mix_histo", true);
    terminator_var     = declare_global_var(module, i64ptrtype, "bf_terminator_count", true);
    mem_intrinsics_var = declare_global_var(module, i64ptrtype, "bf_mem_intrin_count", true);
    load_var       = declare_global_var(module, i64type, "bf_load_count");
    store_var      = declare_global_var(module, i64type, "bf_store_count");
    load_inst_var  = declare_global_var(module, i64type, "bf_load_ins_count");
    store_inst_var = declare_global_var(module, i64type, "bf_store_ins_count");
    flop_var       = declare_global_var(module, i64type, "bf_flop_count");
    fp_bits_var    = declare_global_var(module, i64type, "bf_fp_bits_count");

    op_var         = declare_global_var(module, i64type, "bf_op_count");
    op_bits_var    = declare_global_var(module, i64type, "bf_op_bits_count");

    // Assign a few constant values.
    not_end_of_bb = ConstantInt::get(globctx, APInt(32, 0));
    uncond_end_bb = ConstantInt::get(globctx, APInt(32, 1));
    cond_end_bb = ConstantInt::get(globctx, APInt(32, 2));
    zero = ConstantInt::get(globctx, APInt(64, 0));
    one = ConstantInt::get(globctx, APInt(64, 1));

    // Construct a set of functions to instrument and a set of
    // functions not to instrument.
    instrument_only = parse_function_names(IncludedFunctions);
    dont_instrument = parse_function_names(ExcludedFunctions);
    if (instrument_only && dont_instrument)
      report_fatal_error("-bf-include and -bf-exclude are mutually exclusive");

    // Assign a value to bf_bb_merge.
    create_global_constant(module, "bf_bb_merge", uint64_t(BBMergeCount));

    // Assign a value to bf_every_bb.
    create_global_constant(module, "bf_every_bb", bool(InstrumentEveryBB));

    // Assign a value to bf_types.
    create_global_constant(module, "bf_types", bool(TallyTypes));

    // Assign a value to bf_tally_inst_mix (instruction mix).
    create_global_constant(module, "bf_tally_inst_mix", bool(TallyInstMix));

    // Assign a value to bf_per_func.
    create_global_constant(module, "bf_per_func", bool(TallyByFunction));

    // Assign a value to bf_call_stack.
    if (TrackCallStack && !TallyByFunction)
      report_fatal_error("-bf-call-stack is allowed only in conjuction with -bf-by-func");
    create_global_constant(module, "bf_call_stack", bool(TrackCallStack));

    // Assign a value to bf_unique_bytes.
    create_global_constant(module, "bf_unique_bytes", bool(TrackUniqueBytes));

    // Assign a value to bf_mem_footprint.
    create_global_constant(module, "bf_mem_footprint", bool(FindMemFootprint));

    // Assign a value to bf_vectors.
    create_global_constant(module, "bf_vectors", bool(TallyVectors));

    // Assign a value to bf_max_reuse_dist.
    create_global_constant(module, "bf_max_reuse_distance", uint64_t(MaxReuseDist));

    // Create a global string that stores all of our command-line options.
    ifstream cmdline("/proc/self/cmdline");   // Full command line passed to opt
    string bf_cmdline("[failed to read /proc/self/cmdline]");  // Reconstructed command line with -bf-* options only
    if (cmdline.is_open()) {
      // Read the command line into a buffer.
      const size_t maxcmdlinelen = 65536;
      char cmdline_chars[maxcmdlinelen] = {0};
      cmdline.read(cmdline_chars, maxcmdlinelen);
      cmdline.close();

      // Parse the command line.  Each argument is terminated by a
      // null character, and the command line as a whole is terminated
      // by two null characters.
      if (!cmdline.bad()) {
        char* arg = cmdline_chars;
	bf_cmdline = "";
        while (1) {
          size_t arglen = strlen(arg);
          if (arglen == 0)
            break;
          if (!strncmp(arg, "-bf", 3)) {
            bf_cmdline += ' ';
            bf_cmdline += arg;
          }
          arg += arglen + 1;
        }
      }
    }
    const char *bf_cmdline_str = bf_cmdline.c_str();
    if (bf_cmdline_str[0] == ' ')
      bf_cmdline_str++;
    bf_cmdline_str = strdup(bf_cmdline_str);
    create_global_constant(module, "bf_option_string", bf_cmdline_str);

    /**
     * Instead of using a map with the function names as keys, we associate a unique
     * integer with each function and use that as the key.  We maintain the association
     * of the function names to their integer keys at compile time, then create a
     * constructor to record the map via a call to bf_record_funcs2keys.
     */
    vector<Type*> func_arg;
    func_arg.push_back(IntegerType::get(globctx, 8*sizeof(uint32_t)));
    func_arg.push_back(PointerType::get(IntegerType::get(globctx, 8*sizeof(uint64_t)),0));

    PointerType* char_ptr = PointerType::get(IntegerType::get(globctx, 8), 0);
    PointerType* char_ptr_ptr = PointerType::get(char_ptr, 0);
    func_arg.push_back(char_ptr_ptr);

    FunctionType* void_int_func_result =
            FunctionType::get(Type::getVoidTy(globctx), func_arg, false);

    record_funcs2keys = declare_extern_c(void_int_func_result,
                   "bf_record_funcs2keys",
                   &module);

    // Inject external declarations for
    // bf_initialize_if_necessary(), bf_push_basic_block(), and
    // bf_pop_basic_block().
    init_if_necessary = declare_thunk(&module, "bf_initialize_if_necessary");
    push_bb = declare_thunk(&module, "bf_push_basic_block");
    pop_bb = declare_thunk(&module, "bf_pop_basic_block");

    // Inject external declarations for bf_accumulate_bb_tallies(),
    // bf_reset_bb_tallies(), and bf_report_bb_tallies().
    if (InstrumentEveryBB) {
      accum_bb_tallies = declare_thunk(&module, "bf_accumulate_bb_tallies");
      reset_bb_tallies = declare_thunk(&module, "bf_reset_bb_tallies");
      report_bb_tallies = declare_thunk(&module, "bf_report_bb_tallies");
    }

    // Inject an external declarations for bf_increment_func_tally().
    assoc_counts_with_func = 0;
    tally_function = 0;
    push_function = 0;
    pop_function = 0;

    if (TallyByFunction) {
        // bf_assoc_counters_with_func
        vector<Type*> func_arg;

        // arg: key ID
        func_arg.push_back(IntegerType::get(globctx, 8*sizeof(FunctionKeyGen::KeyID)));
        FunctionType* void_func_result =
          FunctionType::get(Type::getVoidTy(globctx), func_arg, false);
        assoc_counts_with_func =
          declare_extern_c(void_func_result,
                           "bf_assoc_counters_with_func",
                           &module);

        vector<Type*> taly_func_arg;
        taly_func_arg.push_back(PointerType::get(IntegerType::get(globctx, 8), 0));
        // add 2nd arg for function key
        taly_func_arg.push_back(IntegerType::get(globctx, 8*sizeof(FunctionKeyGen::KeyID)));
        FunctionType* void_str_int_func_result =
                  FunctionType::get(Type::getVoidTy(globctx), taly_func_arg, false);

      tally_function =
        declare_extern_c(void_func_result,
                         "bf_incr_func_tally",
                         &module);

      if ( TrackCallStack )
      {
          // Inject external declarations for bf_push_function() and
          // bf_pop_function().
          // bf_push_function()
          push_function =
                  declare_extern_c(void_str_int_func_result,
                                   "bf_push_function",
                                   &module);
          // bf_pop_function()
          pop_function = declare_thunk(&module, "bf_pop_function");
      }
    }

    // Declare bf_tally_vector_operation() only if we were asked
    // to track vector operations.
    if (TallyVectors) {
      vector<Type*> all_function_args;
      all_function_args.push_back(PointerType::get(IntegerType::get(globctx, 8), 0));
      all_function_args.push_back(IntegerType::get(globctx, 64));
      all_function_args.push_back(IntegerType::get(globctx, 64));
      all_function_args.push_back(IntegerType::get(globctx, 8));
      FunctionType* void_func_result =
        FunctionType::get(Type::getVoidTy(globctx), all_function_args, false);
      tally_vector =
        declare_extern_c(void_func_result,
                         "bf_tally_vector_operation",
                         &module);
    }

    // Inject external declarations for bf_assoc_addresses_with_prog()
    // and bf_assoc_addresses_with_func().
    if (TrackUniqueBytes) {
      // Declare bf_assoc_addresses_with_prog() any time we need to
      // track unique bytes.
      vector<Type*> all_function_args;
      all_function_args.push_back(IntegerType::get(globctx, 64));
      all_function_args.push_back(IntegerType::get(globctx, 64));
      FunctionType* void_func_result =
        FunctionType::get(Type::getVoidTy(globctx), all_function_args, false);
      assoc_addrs_with_prog =
        declare_extern_c(void_func_result,
			 FindMemFootprint
			 ? "bf_assoc_addresses_with_prog_tb"
                         : "bf_assoc_addresses_with_prog",
                         &module);

      // Declare bf_assoc_addresses_with_func() only if we were
      // asked to track unique addresses by function.
      if (TallyByFunction) {
        vector<Type*> all_function_args;
        all_function_args.push_back(PointerType::get(IntegerType::get(globctx, 8), 0));
        all_function_args.push_back(IntegerType::get(globctx, 64));
        all_function_args.push_back(IntegerType::get(globctx, 64));
        FunctionType* void_func_result =
          FunctionType::get(Type::getVoidTy(globctx), all_function_args, false);
        assoc_addrs_with_func =
          declare_extern_c(void_func_result,
			   FindMemFootprint
			   ? "bf_assoc_addresses_with_func_tb"
			   : "bf_assoc_addresses_with_func",
                           &module);
      }
    }

    // Inject an external declaration for llvm.memset.p0i8.i64().
    memset_intrinsic = module.getFunction("llvm.memset.p0i8.i64");
    if (memset_intrinsic == NULL) {
      vector<Type*> all_function_args;
      all_function_args.push_back(PointerType::get(IntegerType::get(globctx, 8), 0));
      all_function_args.push_back(IntegerType::get(globctx, 8));
      all_function_args.push_back(IntegerType::get(globctx, 64));
      all_function_args.push_back(IntegerType::get(globctx, 32));
      all_function_args.push_back(IntegerType::get(globctx, 1));
      FunctionType* void_func_result =
        FunctionType::get(Type::getVoidTy(globctx), all_function_args, false);
      memset_intrinsic =
        declare_extern_c(void_func_result,
                         "llvm.memset.p0i8.i64",
                         &module);
    }

    // Simplify ReuseDist.getBits() into rd_bits.
    rd_bits = ReuseDist.getBits();
    if ((rd_bits&(1<<RD_BOTH)) != 0)
      rd_bits = (1<<RD_LOADS) | (1<<RD_STORES);

    // Inject external declarations for bf_reuse_dist_addrs_prog().
    if (rd_bits > 0) {
      vector<Type*> all_function_args;
      all_function_args.push_back(IntegerType::get(globctx, 64));
      all_function_args.push_back(IntegerType::get(globctx, 64));
      FunctionType* void_func_result =
        FunctionType::get(Type::getVoidTy(globctx), all_function_args, false);
      reuse_dist_prog =
        declare_extern_c(void_func_result,
                         "bf_reuse_dist_addrs_prog",
                         &module);
    }

    // Inject external declarations for bf_acquire_mega_lock() and
    // bf_release_mega_lock().
    if (ThreadSafety) {
      take_mega_lock = declare_thunk(&module, "bf_acquire_mega_lock");
      release_mega_lock = declare_thunk(&module, "bf_release_mega_lock");
    }

    // initialize the function key generator
    FunctionKeyGen::Seed_t  seed;
    std::hash<std::string> hash_key;
    seed = hash_key(module.getModuleIdentifier());

    m_keygen = std::unique_ptr<FunctionKeyGen>(new FunctionKeyGen(seed));


    return true;
  }

  char BytesFlops::ID = 0;

  // Insert code for incrementing our byte, flop, etc. counters.
  bool BytesFlops::runOnFunction(Function& function) {
    // Do nothing if we're supposed to ignore this function.

    StringRef function_name = function.getName();
    string function_name_orig = demangle_func_name(function_name.str());
    remove_all_instances(function_name_orig, ' ');  // Normalize the name by removing spaces.

    if ( instrument_only != NULL )
    {
        if ( (instrument_only->find(function_name) == instrument_only->end())
             && (instrument_only->find(function_name_orig) == instrument_only->end())
            )
            return false;
    }

    if ( dont_instrument != NULL )
    {
        if ( (dont_instrument->find(function_name) != dont_instrument->end())
            || (dont_instrument->find(function_name_orig) != dont_instrument->end())
            )
            return false;
    }

    if (function_name == "bf_categorize_counters")
      // Avoid the endless recursion that would be caused if we were
      // to instrument bf_categorize_counters() using
      // bf_categorize_counters().
      return false;

    if ( "bf_initialize_func_map" == function_name )
    {
        return false;
    }

    if ( function.empty() )
    {
//        printf("func %s is empty\n", function_name.str().c_str());
        return false;
    }
    // Reset all of our static counters.
    static_loads = 0;
    static_stores = 0;
    static_flops = 0;
    static_ops = 0;
    static_cond_brs = 0;
    static_bblocks = 0;

    // set up a unique identifier for the function so that we don't
    // have to compute the hash value repeatedly.
//    FunctionKeyGen::KeyID
//    key = m_keygen->nextRandomKey();


    // Instrument "interesting" instructions in every basic block.
    Module* module = function.getParent();
    instrument_entire_function(module, function, function_name);

//    printf("done with %s\n", function_name.str().c_str());
    // Return, indicating that we modified this function.
    return true;
  }


  bool BytesFlops::runOnModule(Module & module)
  {
      doInitialization(module);

      /*
      printf("Module %s\n", module.getModuleIdentifier().c_str());
      if ( module.getModuleIdentifier() != "foo.s" )
          return false;
       */
      
      for ( auto fiter = module.begin(); fiter != module.end(); fiter++ )
      {
//          printf("processing function %s\n", fiter->getName().str().c_str());
          runOnFunction(*fiter);
      }

      doFinalization(module);

      return true;
  }

  // Output what we instrumented.
  void BytesFlops::print(raw_ostream &outfile, const Module *module) const {
    outfile << module->getModuleIdentifier() << ": "
            << static_loads << " loads, "
            << static_stores << " stores, "
            << static_flops << " flops, "
            << static_cond_brs << " cond_brs, "
            << static_ops << " total_ops, "
            << static_bblocks << " bblocks\n";
  }

  // Make the BytesFlops pass run automatically when loaded via Clang.
  // This technique comes from "Run an LLVM Pass Automatically with
  // Clang" (http://homes.cs.washington.edu/~asampson/blog/clangpass.html).
  //
  // We want our pass to run as late as possible.  Unfortunately,
  // EP_OptimizerLast is leading to segmentation faults so we use
  // EP_ScalarOptimizerLate instead.  However, that doesn't run when
  // optimizations are disabled so we additionally have to register
  // our pass to run with EP_EnabledOnOptLevel0.  Run-time checks of
  // the optimization level ensure that the pass executes exactly
  // once.
  static void registerByflPass_O0(const PassManagerBuilder& pass_mgr_builder,
                                  PassManagerBase& pass_mgr) {
    if (pass_mgr_builder.OptLevel == 0)
    {
      pass_mgr.add(new BytesMP());
      pass_mgr.add(new BytesFlops());
    }
  }
  static RegisterStandardPasses
  RegisterByflPass_O0(PassManagerBuilder::EP_EnabledOnOptLevel0, registerByflPass_O0);
  static void registerByflPass_opt(const PassManagerBuilder& pass_mgr_builder,
                                  PassManagerBase& pass_mgr) {
    if (pass_mgr_builder.OptLevel > 0)
    {
      pass_mgr.add(new BytesMP());
      pass_mgr.add(new BytesFlops());
    }
  }
  static RegisterStandardPasses
  RegisterByflPass_opt(PassManagerBuilder::EP_ScalarOptimizerLate, registerByflPass_opt);

}  // namespace bytesflops_pass
