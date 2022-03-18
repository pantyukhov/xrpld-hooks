#ifndef APPLY_HOOK_INCLUDED
#define APPLY_HOOK_INCLUDED 1
#include <ripple/basics/Blob.h>
#include <ripple/protocol/TER.h>
#include <ripple/app/tx/impl/ApplyContext.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/protocol/SField.h>
#include <queue>
#include <optional>
#include <any>
#include <memory>
#include <vector>
#include <ripple/protocol/digest.h>
#include <ripple/app/tx/applyHookMacro.h>
#include <wasmedge/wasmedge.h>

enum HookSetFlags : uint8_t
{
    hsfOVERRIDE = (1U << 0U),       // override or delete hook
    hsfNSDELETE = (1U << 1U),       // delete namespace
};

enum HookSetOperation : int8_t
{
    hsoINVALID  = -1,
    hsoNOOP     = 0,
    hsoCREATE   = 1,
    hsoINSTALL  = 2,
    hsoDELETE   = 3,
    hsoNSDELETE = 4,
    hsoUPDATE   = 5
};

namespace hook
{
    struct HookContext;
    struct HookResult;
    bool isEmittedTxn(ripple::STTx const& tx);


    // this map type acts as both a read and write cache for hook execution
    using HookStateMap =    std::map<ripple::AccountID,             // account that owns the state
                         std::map<ripple::uint256,               // namespace
                            std::map<ripple::uint256,               // key
                            std::pair<
                                bool,                               // is modified from ledger value
                                ripple::Blob>>>>;                   // the value

    enum TSHFlags : uint8_t
    {
        tshNONE                = 0b000,
        tshROLLBACK            = 0b001,
        tshCOLLECT             = 0b010,
    };

    using namespace ripple;
    static const std::map<uint16_t, uint8_t> TSHAllowances = 
    {
        {ttPAYMENT,             tshROLLBACK },
        {ttESCROW_CREATE,       tshROLLBACK },
        {ttESCROW_FINISH,       tshROLLBACK },
        {ttACCOUNT_SET,         tshNONE     },
        {ttESCROW_CANCEL,       tshCOLLECT  },
        {ttREGULAR_KEY_SET,     tshNONE     },
//        {ttNICKNAME_SET,        tshNONE     },
        {ttOFFER_CREATE,        tshCOLLECT  },
        {ttOFFER_CANCEL,        tshNONE     },
//        {ttCONTRACT,            tshNONE     },
        {ttTICKET_CREATE,       tshNONE     },
//        {ttSPINAL_TAP,          tshNONE     },
        {ttSIGNER_LIST_SET,     tshROLLBACK },
        {ttPAYCHAN_CREATE,      tshROLLBACK },
        {ttPAYCHAN_FUND,        tshCOLLECT  },
        {ttPAYCHAN_CLAIM,       tshCOLLECT  },
        {ttCHECK_CREATE,        tshROLLBACK },
        {ttCHECK_CASH,          tshROLLBACK },
        {ttCHECK_CANCEL,        tshCOLLECT  },
        {ttDEPOSIT_PREAUTH,     tshROLLBACK },
        {ttTRUST_SET,           tshCOLLECT  },
        {ttACCOUNT_DELETE,      tshROLLBACK },
        {ttHOOK_SET,            tshNONE     }
        // RH TODO: add NFT transactions here
    };


    std::vector<std::pair<AccountID, bool>>
    getTransactionalStakeHolders(STTx const& tx, ReadView const& rv);

    namespace log
    {
        /*
         * Hook log-codes are not necessarily errors. Each type of Hook log line contains a code in
         * round parens like so:
         *      HookSet(5)[rAcc...]: message
         * The log-code gives an external tool an easy way to handle and report the status of a hook
         * to end users and developers.
         */
        enum hook_log_code : uint16_t
        {
            /* HookSet log-codes */
            SHORT_HOOK          =   0,  // web assembly byte code ended abruptly
            CALL_ILLEGAL        =   1,  // wasm tries to call a non-whitelisted function
            GUARD_PARAMETERS    =   2,  // guard called but did not use constant expressions for params
            CALL_INDIRECT       =   3,  // wasm used call indirect instruction which is disallowed
            GUARD_MISSING       =   4,  // guard call missing at top of loop
            MEMORY_GROW         =   5,  // memory.grow instruction is present but disallowed
            BLOCK_ILLEGAL       =   6,  // a block end instruction moves execution below depth 0 {{}}`}` <= like this
            INSTRUCTION_COUNT   =   7,  // worst case execution instruction count as computed by HookSet
            INSTRUCTION_EXCESS  =   8,  // worst case execution instruction count was too large
            PARAMETERS_ILLEGAL  =   9,  // HookParameters contained something other than a HookParameter
            PARAMETERS_FIELD    =  10,  // HookParameters contained a HookParameter with an invalid key in it
            PARAMETERS_NAME     =  11,  // HookParameters contained a HookParameter which lacked ParameterName field
            HASH_OR_CODE        =  12,  // HookSet object can contain only one of CreateCode and HookHash
            GRANTS_EMPTY        =  13,  // HookSet object contained an empty grants array (you should remove it)
            GRANTS_EXCESS       =  14,  // HookSet object cotnained a grants array with too many grants
            GRANTS_ILLEGAL      =  15,  // Hookset object contained grants array which contained a non Grant object
            GRANTS_FIELD        =  16,  // HookSet object contained a grant without Authorize or HookHash
            API_ILLEGAL         =  17,  // HookSet object contained HookApiVersion for existing HookDefinition
            NAMESPACE_MISSING   =  18,  // HookSet object lacked HookNamespace
            API_MISSING         =  19,  // HookSet object did not contain HookApiVersion but should have
            API_INVALID         =  20,  // HookSet object contained HookApiVersion for unrecognised hook API
            HOOKON_MISSING      =  21,  // HookSet object did not contain HookOn but should have
            DELETE_FIELD        =  22,  // 
            OVERRIDE_MISSING    =  23,  // HookSet object was trying to update or delete a hook but lacked hsfOVERRIDE
            FLAGS_INVALID       =  24,  // HookSet flags were invalid for specified operation
            NSDELETE_FIELD      =  25,
            NSDELETE_FLAGS      =  26
            //RH UPTO

        };
    };
    
}

namespace hook_api
{

#define TER_TO_HOOK_RETURN_CODE(x)\
    (((TERtoInt(x)) << 16)*-1)


// for debugging if you want a lot of output change these to if (1)
#define HOOK_DBG 1
#define DBG_PRINTF if (HOOK_DBG) printf
#define DBG_FPRINTF if (HOOK_DBG) fprintf

    namespace keylet_code
    {
        enum keylet_code : uint32_t
        {
            HOOK = 1,
            HOOK_STATE = 2,
            ACCOUNT = 3,
            AMENDMENTS = 4,
            CHILD = 5,
            SKIP = 6,
            FEES = 7,
            NEGATIVE_UNL = 8,
            LINE = 9,
            OFFER = 10,
            QUALITY = 11,
            EMITTED_DIR = 12,
            TICKET = 13,
            SIGNERS = 14,
            CHECK = 15,
            DEPOSIT_PREAUTH = 16,
            UNCHECKED = 17,
            OWNER_DIR = 18,
            PAGE = 19,
            ESCROW = 20,
            PAYCHAN = 21,
            EMITTED = 22
        };
    }

    namespace compare_mode {
        enum compare_mode : uint32_t
        {
            EQUAL = 1,
            LESS = 2,
            GREATER = 4
        };
    }

    enum hook_return_code : int64_t
    {
        SUCCESS = 0,                    // return codes > 0 are reserved for hook apis to return "success"
        OUT_OF_BOUNDS = -1,             // could not read or write to a pointer to provided by hook
        INTERNAL_ERROR = -2,            // eg directory is corrupt
        TOO_BIG = -3,                   // something you tried to store was too big
        TOO_SMALL = -4,                 // something you tried to store or provide was too small
        DOESNT_EXIST = -5,              // something you requested wasn't found
        NO_FREE_SLOTS = -6,             // when trying to load an object there is a maximum of 255 slots
        INVALID_ARGUMENT = -7,          // self explanatory
        ALREADY_SET = -8,               // returned when a one-time parameter was already set by the hook
        PREREQUISITE_NOT_MET = -9,      // returned if a required param wasn't set, before calling
        FEE_TOO_LARGE = -10,            // returned if the attempted operation would result in an absurd fee
        EMISSION_FAILURE = -11,         // returned if an emitted tx was not accepted by rippled
        TOO_MANY_NONCES = -12,          // a hook has a maximum of 256 nonces
        TOO_MANY_EMITTED_TXN = -13,     // a hook has emitted more than its stated number of emitted txn
        NOT_IMPLEMENTED = -14,          // an api was called that is reserved for a future version
        INVALID_ACCOUNT = -15,          // an api expected an account id but got something else
        GUARD_VIOLATION = -16,          // a guarded loop or function iterated over its maximum
        INVALID_FIELD = -17,            // the field requested is returning sfInvalid
        PARSE_ERROR = -18,              // hook asked hookapi to parse something the contents of which was invalid
        RC_ROLLBACK = -19,              // hook should terminate due to a rollback() call
        RC_ACCEPT = -20,                // hook should temrinate due to an accept() call
        NO_SUCH_KEYLET = -21,           // invalid keylet or keylet type
        NOT_AN_ARRAY = -22,             // if a count of an sle is requested but its not STI_ARRAY
        NOT_AN_OBJECT = -23,            // if a subfield is requested from something that isn't an object
        INVALID_FLOAT = -10024,         // specially selected value that will never be a valid exponent
        DIVISION_BY_ZERO = -25,
        MANTISSA_OVERSIZED = -26,
        MANTISSA_UNDERSIZED = -27,
        EXPONENT_OVERSIZED = -28,
        EXPONENT_UNDERSIZED = -29,
        OVERFLOW = -30,                 // if an operation with a float results in an overflow
        NOT_IOU_AMOUNT = -31,
        NOT_AN_AMOUNT = -32,
        CANT_RETURN_NEGATIVE = -33,
        NOT_AUTHORIZED = -34,
        PREVIOUS_FAILURE_PREVENTS_RETRY = -35,
        TOO_MANY_PARAMS = -36
    };

    enum ExitType : uint8_t
    {
        UNSET = 0,
        WASM_ERROR = 1,
        ROLLBACK = 2,
        ACCEPT = 3,
    };

    const int etxn_details_size = 105;
    const int max_slots = 255;
    const int max_nonce = 255;
    const int max_emit = 255;
    const int max_params = 16;
    const int drops_per_byte = 31250; //RH TODO make these  votable config option
    const double fee_base_multiplier = 1.1f;



    // RH NOTE: Find descriptions of api functions in ./impl/applyHook.cpp and hookapi.h (include for hooks)

    static const std::set<std::string> import_whitelist
    {
        "accept",
        "emit",
        "etxn_burden",
        "etxn_details",
        "etxn_fee_base",
        "etxn_generation",
        "etxn_reserve",
        "float_compare",
        "float_divide",
        "float_exponent",
        "float_exponent_set",
        "float_invert",
        "float_mantissa",
        "float_mantissa_set",
        "float_mulratio",
        "float_multiply",
        "float_int",
        "float_negate",
        "float_one",
        "float_set",
        "float_sign",
        "float_sign_set",
        "float_sto",
        "float_sto_set",
        "float_sum",
        "fee_base",
        "_g",
        "hook_account",
        "hook_hash",
        "ledger_seq",
        "ledger_last_hash",
        "nonce",
        "otxn_burden",
        "otxn_field",
        "otxn_slot",
        "otxn_generation",
        "otxn_id",
        "otxn_type",
        "rollback",
        "slot",
        "slot_clear",
        "slot_count",
        "slot_id",
        "slot_set",
        "slot_size",
        "slot_subarray",
        "slot_subfield",
        "slot_type",
        "slot_float",
        "state",
        "state_foreign",
        "state_set",
        "state_foreign_set",
        "trace",
        "trace_num",
        "trace_float",
        "trace_slot",
        "util_accid",
        "util_raddr",
        "util_sha512h",
        "util_verify",
        "sto_subarray",
        "sto_subfield",
        "sto_validate",
        "sto_emplace",
        "sto_erase",
        "util_keylet",
        "hook_pos",
        "hook_param",
        "hook_param_set",
        "hook_skip"
    };

    DECLARE_HOOK_FUNCTION(int32_t,  _g,                 uint32_t guard_id, uint32_t maxiter );

    DECLARE_HOOK_FUNCTION(int64_t,	accept,             uint32_t read_ptr,  uint32_t read_len, int64_t error_code );
    DECLARE_HOOK_FUNCTION(int64_t,	rollback,           uint32_t read_ptr,  uint32_t read_len, int64_t error_code );
    DECLARE_HOOK_FUNCTION(int64_t,	util_raddr,         uint32_t write_ptr, uint32_t write_len,
                                                        uint32_t read_ptr,  uint32_t read_len );
    DECLARE_HOOK_FUNCTION(int64_t,	util_accid,         uint32_t write_ptr, uint32_t write_len,
                                                        uint32_t read_ptr,  uint32_t read_len );
    DECLARE_HOOK_FUNCTION(int64_t,	util_verify,        uint32_t dread_ptr, uint32_t dread_len,
                                                        uint32_t sread_ptr, uint32_t sread_len,
                                                        uint32_t kread_ptr, uint32_t kread_len );
    DECLARE_HOOK_FUNCTION(int64_t,	sto_validate,       uint32_t tread_ptr, uint32_t tread_len );
    DECLARE_HOOK_FUNCTION(int64_t,	sto_subfield,       uint32_t read_ptr,  uint32_t read_len,  uint32_t field_id );
    DECLARE_HOOK_FUNCTION(int64_t,	sto_subarray,       uint32_t read_ptr,  uint32_t read_len,  uint32_t array_id );
    DECLARE_HOOK_FUNCTION(int64_t,	sto_emplace,        uint32_t write_ptr, uint32_t write_len,
                                                        uint32_t sread_ptr, uint32_t sread_len,
                                                        uint32_t fread_ptr, uint32_t fread_len, uint32_t field_id );
    DECLARE_HOOK_FUNCTION(int64_t,  sto_erase,          uint32_t write_ptr, uint32_t write_len,
                                                        uint32_t read_ptr,  uint32_t read_len,  uint32_t field_id );

    DECLARE_HOOK_FUNCTION(int64_t,	util_sha512h,       uint32_t write_ptr, uint32_t write_len,
                                                        uint32_t read_ptr,  uint32_t read_len );
    DECLARE_HOOK_FUNCTION(int64_t,  util_keylet,        uint32_t write_ptr, uint32_t write_len, uint32_t keylet_type,
                                                        uint32_t a,         uint32_t b,         uint32_t c,
                                                        uint32_t d,         uint32_t e,         uint32_t f );
    DECLARE_HOOK_FUNCNARG(int64_t,	etxn_burden         );
    DECLARE_HOOK_FUNCTION(int64_t,	etxn_details,       uint32_t write_ptr, uint32_t write_len );
    DECLARE_HOOK_FUNCTION(int64_t,	etxn_fee_base,      uint32_t tx_byte_count);
    DECLARE_HOOK_FUNCTION(int64_t,	etxn_reserve,       uint32_t count );
    DECLARE_HOOK_FUNCNARG(int64_t,	etxn_generation     );
    DECLARE_HOOK_FUNCTION(int64_t,	emit,               uint32_t write_ptr, uint32_t write_len,
                                                        uint32_t read_ptr,  uint32_t read_len );

    DECLARE_HOOK_FUNCTION(int64_t,  float_set,          int32_t exponent,   int64_t mantissa );
    DECLARE_HOOK_FUNCTION(int64_t,  float_multiply,     int64_t float1,     int64_t float2 );
    DECLARE_HOOK_FUNCTION(int64_t,  float_mulratio,     int64_t float1,     uint32_t round_up,
                                                        uint32_t numerator, uint32_t denominator );
    DECLARE_HOOK_FUNCTION(int64_t,  float_negate,       int64_t float1 );
    DECLARE_HOOK_FUNCTION(int64_t,  float_compare,      int64_t float1,     int64_t float2, uint32_t mode );
    DECLARE_HOOK_FUNCTION(int64_t,  float_sum,          int64_t float1,     int64_t float2 );
    DECLARE_HOOK_FUNCTION(int64_t,  float_sto,          uint32_t write_ptr, uint32_t write_len,
                                                        uint32_t cread_ptr, uint32_t cread_len,
                                                        uint32_t iread_ptr, uint32_t iread_len,
                                                        int64_t float1,     uint32_t field_code);
    DECLARE_HOOK_FUNCTION(int64_t,  float_sto_set,      uint32_t read_ptr,  uint32_t read_len );
    DECLARE_HOOK_FUNCTION(int64_t,  float_invert,       int64_t float1 );
    DECLARE_HOOK_FUNCTION(int64_t,  float_divide,       int64_t float1,     int64_t float2 );
    DECLARE_HOOK_FUNCNARG(int64_t,  float_one );

    DECLARE_HOOK_FUNCTION(int64_t,  float_exponent,     int64_t float1 );
    DECLARE_HOOK_FUNCTION(int64_t,  float_exponent_set, int64_t float1,     int32_t exponent );
    DECLARE_HOOK_FUNCTION(int64_t,  float_mantissa,     int64_t float1 );
    DECLARE_HOOK_FUNCTION(int64_t,  float_mantissa_set, int64_t float1,     int64_t mantissa );
    DECLARE_HOOK_FUNCTION(int64_t,  float_sign,         int64_t float1 );
    DECLARE_HOOK_FUNCTION(int64_t,  float_sign_set,     int64_t float1,     uint32_t negative );
    DECLARE_HOOK_FUNCTION(int64_t,  float_int,          int64_t float1,     uint32_t decimal_places, uint32_t abs );

    DECLARE_HOOK_FUNCTION(int64_t,	hook_account,       uint32_t write_ptr, uint32_t write_len );
    DECLARE_HOOK_FUNCTION(int64_t,	hook_hash,          uint32_t write_ptr, uint32_t write_len, int32_t hook_no );
    DECLARE_HOOK_FUNCNARG(int64_t,	fee_base            );
    DECLARE_HOOK_FUNCNARG(int64_t,	ledger_seq          );
    DECLARE_HOOK_FUNCTION(int64_t,  ledger_last_hash,   uint32_t write_ptr, uint32_t write_len );
    DECLARE_HOOK_FUNCTION(int64_t,	nonce,              uint32_t write_ptr, uint32_t write_len );


    DECLARE_HOOK_FUNCTION(int64_t,  hook_param_set,     uint32_t read_ptr,  uint32_t read_len,
                                                        uint32_t kread_ptr, uint32_t kread_len,
                                                        uint32_t hread_ptr, uint32_t hread_len);

    DECLARE_HOOK_FUNCTION(int64_t,  hook_param,         uint32_t write_ptr, uint32_t write_len,
                                                        uint32_t read_ptr,  uint32_t read_len);

    DECLARE_HOOK_FUNCTION(int64_t,  hook_skip,          uint32_t read_ptr,  uint32_t read_len, uint32_t flags);
    DECLARE_HOOK_FUNCNARG(int64_t,  hook_pos);

    DECLARE_HOOK_FUNCTION(int64_t,	slot,               uint32_t write_ptr, uint32_t write_len, uint32_t slot );
    DECLARE_HOOK_FUNCTION(int64_t,	slot_clear,         uint32_t slot );
    DECLARE_HOOK_FUNCTION(int64_t,	slot_count,         uint32_t slot );
    DECLARE_HOOK_FUNCTION(int64_t,	slot_id,            uint32_t write_ptr, uint32_t write_len, uint32_t slot );
    DECLARE_HOOK_FUNCTION(int64_t,	slot_set,           uint32_t read_ptr,  uint32_t read_len, int32_t slot );
    DECLARE_HOOK_FUNCTION(int64_t,	slot_size,          uint32_t slot );
    DECLARE_HOOK_FUNCTION(int64_t,	slot_subarray,      uint32_t parent_slot, uint32_t array_id, uint32_t new_slot );
    DECLARE_HOOK_FUNCTION(int64_t,	slot_subfield,      uint32_t parent_slot, uint32_t field_id, uint32_t new_slot );
    DECLARE_HOOK_FUNCTION(int64_t,	slot_type,          uint32_t slot_no, uint32_t flags );
    DECLARE_HOOK_FUNCTION(int64_t,	slot_float,         uint32_t slot_no );

    DECLARE_HOOK_FUNCTION(int64_t,	state_set,          uint32_t read_ptr,  uint32_t read_len,
                                                        uint32_t kread_ptr, uint32_t kread_len );
    DECLARE_HOOK_FUNCTION(int64_t,	state_foreign_set,  uint32_t read_ptr,  uint32_t read_len,
                                                        uint32_t kread_ptr, uint32_t kread_len,
                                                        uint32_t nread_ptr, uint32_t nread_len,
                                                        uint32_t aread_ptr, uint32_t aread_len );
    DECLARE_HOOK_FUNCTION(int64_t,	state,              uint32_t write_ptr, uint32_t write_len,
                                                        uint32_t kread_ptr, uint32_t kread_len );
    DECLARE_HOOK_FUNCTION(int64_t,	state_foreign,      uint32_t write_ptr, uint32_t write_len,
                                                        uint32_t kread_ptr, uint32_t kread_len,
                                                        uint32_t nread_ptr, uint32_t nread_len,
                                                        uint32_t aread_ptr, uint32_t aread_len );
    DECLARE_HOOK_FUNCTION(int64_t,	trace_slot,         uint32_t read_ptr, uint32_t read_len, uint32_t slot );
    DECLARE_HOOK_FUNCTION(int64_t,	trace,              uint32_t mread_ptr, uint32_t mread_len,
                                                        uint32_t dread_ptr, uint32_t dread_len, uint32_t as_hex );
    DECLARE_HOOK_FUNCTION(int64_t,	trace_num,          uint32_t read_ptr, uint32_t read_len, int64_t number );
    DECLARE_HOOK_FUNCTION(int64_t,	trace_float,        uint32_t read_ptr, uint32_t read_len, int64_t  float1 );

    DECLARE_HOOK_FUNCNARG(int64_t,	otxn_burden         );
    DECLARE_HOOK_FUNCTION(int64_t,	otxn_field,         uint32_t write_ptr, uint32_t write_len, uint32_t field_id );
    DECLARE_HOOK_FUNCTION(int64_t,	otxn_field_txt,     uint32_t write_ptr, uint32_t write_len, uint32_t field_id );
    DECLARE_HOOK_FUNCNARG(int64_t,	otxn_generation     );
    DECLARE_HOOK_FUNCTION(int64_t,	otxn_id,            uint32_t write_ptr, uint32_t write_len, uint32_t flags );
    DECLARE_HOOK_FUNCNARG(int64_t,	otxn_type           );
    DECLARE_HOOK_FUNCTION(int64_t,	otxn_slot,          uint32_t slot_no );


} /* end namespace hook_api */

namespace hook
{

    bool canHook(ripple::TxType txType, uint64_t hookOn);

    struct HookResult;

    HookResult
    apply(
        ripple::uint256 const& hookSetTxnID, /* this is the txid of the sethook, used for caching (one day) */
        ripple::uint256 const& hookHash,     /* hash of the actual hook byte code, used for metadata */
        ripple::uint256 const& hookNamespace,
        ripple::Blob const& wasm,
        std::map<
            std::vector<uint8_t>,          /* param name  */
            std::vector<uint8_t>           /* param value */
            > const& hookParams,
        std::map<
            ripple::uint256,          /* hook hash */
            std::map<
                std::vector<uint8_t>,
                std::vector<uint8_t>
            >> const& hookParamOverrides,
        std::shared_ptr<HookStateMap>& stateMap,
        ripple::ApplyContext& applyCtx,
        ripple::AccountID const& account,     /* the account the hook is INSTALLED ON not always the otxn account */
        bool callback = false,
        uint32_t wasmParam = 0,
        int32_t hookChainPosition = -1
    );

    struct HookContext;

    uint32_t maxHookStateDataSize(void);
    uint32_t maxHookWasmSize(void);
    uint32_t maxHookParameterKeySize(void);
    uint32_t maxHookParameterValueSize(void);

    uint32_t maxHookChainLength(void);

    uint32_t computeExecutionFee(uint64_t instructionCount);
    uint32_t computeCreationFee(uint64_t byteCount);

    struct HookResult
    {
        ripple::uint256     const&      hookSetTxnID;
        ripple::uint256     const&      hookHash;
        ripple::Keylet      const&      accountKeylet;
        ripple::Keylet      const&      ownerDirKeylet;
        ripple::Keylet      const&      hookKeylet;
        ripple::AccountID   const&      account;
        ripple::AccountID   const&      otxnAccount;
        ripple::uint256     const&      hookNamespace;

        std::queue<std::shared_ptr<ripple::Transaction>> emittedTxn {}; // etx stored here until accept/rollback
        std::shared_ptr<HookStateMap> stateMap;
        uint16_t changedStateCount = 0;
        std::map<
            ripple::uint256,                    // hook hash
            std::map<
                std::vector<uint8_t>,                // hook param name
                std::vector<uint8_t>                 // hook param value
            >> hookParamOverrides;

        std::map<
            std::vector<uint8_t>,
            std::vector<uint8_t>>
                const& hookParams;
        std::set<ripple::uint256> hookSkips;
        hook_api::ExitType exitType = hook_api::ExitType::ROLLBACK;
        std::string exitReason {""};
        int64_t exitCode {-1};
        uint64_t instructionCount {0};
        bool callback = false;
        uint32_t wasmParam = 0;
        uint32_t overrideCount = 0;
        int32_t hookChainPosition = -1;
        bool foreignStateSetDisabled = false;
    };

    class HookExecutor;

    struct SlotEntry
    {
        std::vector<uint8_t> id;
        std::shared_ptr<const ripple::STObject> storage;
        const ripple::STBase* entry; // raw pointer into the storage, that can be freely pointed around inside
    };

    struct HookContext {
        ripple::ApplyContext& applyCtx;
        // slots are used up by requesting objects from inside the hook
        // the map stores pairs consisting of a memory view and whatever shared or unique ptr is required to
        // keep the underlying object alive for the duration of the hook's execution
        // slot number -> { keylet or hash, { pointer to current object, storage for that object } }
        std::map<int, SlotEntry> slot {};
        int slot_counter { 1 };
        std::queue<int> slot_free {};
        int64_t expected_etxn_count { -1 }; // make this a 64bit int so the uint32 from the hookapi cant overflow it
        int nonce_counter { 0 }; // incremented whenever nonce is called to ensure unique nonces
        std::map<ripple::uint256, bool> nonce_used {};
        uint32_t generation = 0; // used for caching, only generated when txn_generation is called
        int64_t burden = 0;      // used for caching, only generated when txn_burden is called
        int64_t fee_base = 0;
        std::map<uint32_t, uint32_t> guard_map {}; // iteration guard map <id -> upto_iteration>
        HookResult result;
        std::optional<ripple::STObject> emitFailure;    // if this is a callback from a failed
                                                        // emitted txn then this optional becomes
                                                        // populated with the SLE
        const HookExecutor* module = 0;
    };


    ripple::TER
    setHookState(
        ripple::ApplyContext& applyCtx,
        ripple::AccountID const& acc,
        ripple::uint256 const& ns,
        ripple::uint256 const & key,
        ripple::Slice const& data);


    // write hook execution metadata and remove emitted transaction ledger entries
    ripple::TER
    finalizeHookResult(
        hook::HookResult& hookResult,
        ripple::ApplyContext&,
        bool doEmit);

    // write state map to ledger
    ripple::TER
    finalizeHookState(
        std::shared_ptr<HookStateMap>&,
        ripple::ApplyContext&,
        ripple::uint256 const&);

    // if the txn being executed was an emitted txn then this removes it from the emission directory
    ripple::TER
    removeEmissionEntry(
        ripple::ApplyContext& applyCtx);
    
    // RH TODO: call destruct for these on rippled shutdown
    #define ADD_HOOK_FUNCTION(F, ctx)\
    {\
        WasmEdge_FunctionInstanceContext* hf = WasmEdge_FunctionInstanceCreate(\
                hook_api::WasmFunctionType##F,\
                hook_api::WasmFunction##F,\
                (void*)(&ctx), 0);\
        WasmEdge_ImportObjectAddFunction(importObj, hook_api::WasmFunctionName##F, hf);\
    }

    #define HR_ACC() hookResult.account << "-" << hookResult.otxnAccount
    #define HC_ACC() hookCtx.result.account << "-" << hookCtx.result.otxnAccount

    // create these once at boot and keep them
    static WasmEdge_String exportName = WasmEdge_StringCreateByCString("env");
    static WasmEdge_String tableName = WasmEdge_StringCreateByCString("table");
    static auto* tableType =
        WasmEdge_TableTypeCreate(WasmEdge_RefType_FuncRef, {.HasMax = true, .Min = 10, .Max = 20});
    static auto* memType = WasmEdge_MemoryTypeCreate({.HasMax = true, .Min = 1, .Max = 1});
    static WasmEdge_String memName = WasmEdge_StringCreateByCString("memory");
    static WasmEdge_String cbakFunctionName = WasmEdge_StringCreateByCString("cbak");
    static WasmEdge_String hookFunctionName = WasmEdge_StringCreateByCString("hook");

    // see: lib/system/allocator.cpp
    #define WasmEdge_kPageSize 65536ULL

    /**
     * HookExecutor is effectively a two-part function:
     * The first part sets up the Hook Api inside the wasm import, ready for use
     * (this is done during object construction.)
     * The second part is actually executing webassembly instructions
     * this is done during execteWasm function.
     * The instance is single use.
     */
    class HookExecutor
    {

        private:
            bool spent = false; // a HookExecutor can only be used once


        public:
            HookContext hookCtx;
            WasmEdge_ImportObjectContext* importObj;

        /**
         * Validate that a web assembly blob can be loaded by wasmedge
         */
        static std::optional<std::string> validateWasm(const void* wasm, size_t len)
        {
            std::optional<std::string> ret;
            WasmEdge_ConfigureContext* confCtx  = WasmEdge_ConfigureCreate();
            WasmEdge_VMContext* vmCtx = WasmEdge_VMCreate(confCtx, NULL);
            WasmEdge_Result res = WasmEdge_VMLoadWasmFromBuffer(vmCtx, reinterpret_cast<const uint8_t*>(wasm), len);
            if (!WasmEdge_ResultOK(res))
                *ret = std::string {WasmEdge_ResultGetMessage(res)};
            else
            {
                res = WasmEdge_VMValidate(vmCtx);
                if (!WasmEdge_ResultOK(res))
                    *ret = std::string {WasmEdge_ResultGetMessage(res)};
            }
            WasmEdge_VMDelete(vmCtx);
            WasmEdge_ConfigureDelete(confCtx);
            return ret;
        }

        /**
         * Execute web assembly byte code against the constructed Hook Context
         * Once execution has occured the exector is spent and cannot be used again and should be destructed
         * Information about the execution is populated into hookCtx
         */
        void executeWasm(const void* wasm, size_t len, bool callback, uint32_t wasmParam, beast::Journal const& j)
        {

            // HookExecutor can only execute once
            assert(!spent);

            spent = true;

            JLOG(j.trace())
                << "HookInfo[" << HC_ACC() << "]: creating wasm instance";

            WasmEdge_ConfigureContext* confCtx  = WasmEdge_ConfigureCreate();
            WasmEdge_ConfigureStatisticsSetInstructionCounting(confCtx, true);
            WasmEdge_VMContext* vmCtx = WasmEdge_VMCreate(confCtx, NULL);

            WasmEdge_Result res = WasmEdge_VMRegisterModuleFromImport(vmCtx, this->importObj);
            if (!WasmEdge_ResultOK(res))
            {
                hookCtx.result.exitType = hook_api::ExitType::WASM_ERROR;
                JLOG(j.trace())
                    << "HookError[" << HC_ACC() << "]: Import phase failed "
                    << WasmEdge_ResultGetMessage(res);
            }
            else
            {

                WasmEdge_Value params[1] = { WasmEdge_ValueGenI32((int64_t)wasmParam) };
                WasmEdge_Value returns[1];

                /*
                printf("executing hook wasm:\n");
                for (int j = 0; j < len; j++)
                {
                    if (j % 16 == 0)
                        printf("0x%08X:\t", j);

                    printf("%02X%s", (reinterpret_cast<const uint8_t*>(wasm))[j],
                        (j % 16 == 15 ? "\n" :
                        (j % 4 == 3 ? "  " :
                        (j % 2 == 1 ? " " : ""))));
                }
                printf("\n----\n");
                */

                res =
                    WasmEdge_VMRunWasmFromBuffer(vmCtx, reinterpret_cast<const uint8_t*>(wasm), len,
                        callback ? cbakFunctionName : hookFunctionName,
                        params, 1, returns, 1);

                if (!WasmEdge_ResultOK(res))
                {
                    JLOG(j.warn())
                        << "HookError[" << HC_ACC() << "]: WASM VM error "
                        <<  WasmEdge_ResultGetMessage(res);
                    hookCtx.result.exitType = hook_api::ExitType::WASM_ERROR;
                }
                else
                {

                    auto* statsCtx= WasmEdge_VMGetStatisticsContext(vmCtx);
                    hookCtx.result.instructionCount = WasmEdge_StatisticsGetInstrCount(statsCtx);
                }
            }

            WasmEdge_ConfigureDelete(confCtx);
            WasmEdge_VMDelete(vmCtx);
        }

        HookExecutor(HookContext& ctx)
            : hookCtx(ctx)
            , importObj(WasmEdge_ImportObjectCreate(exportName))
        {
            ctx.module = this;

            WasmEdge_LogSetDebugLevel();

            ADD_HOOK_FUNCTION(_g, ctx);
            ADD_HOOK_FUNCTION(accept, ctx);
            ADD_HOOK_FUNCTION(rollback, ctx);
            ADD_HOOK_FUNCTION(util_raddr, ctx);
            ADD_HOOK_FUNCTION(util_accid, ctx);
            ADD_HOOK_FUNCTION(util_verify, ctx);
            ADD_HOOK_FUNCTION(util_sha512h, ctx);
            ADD_HOOK_FUNCTION(sto_validate, ctx);
            ADD_HOOK_FUNCTION(sto_subfield, ctx);
            ADD_HOOK_FUNCTION(sto_subarray, ctx);
            ADD_HOOK_FUNCTION(sto_emplace, ctx);
            ADD_HOOK_FUNCTION(sto_erase, ctx);
            ADD_HOOK_FUNCTION(util_keylet, ctx);

            ADD_HOOK_FUNCTION(emit, ctx);
            ADD_HOOK_FUNCTION(etxn_burden, ctx);
            ADD_HOOK_FUNCTION(etxn_fee_base, ctx);
            ADD_HOOK_FUNCTION(etxn_details, ctx);
            ADD_HOOK_FUNCTION(etxn_reserve, ctx);
            ADD_HOOK_FUNCTION(etxn_generation, ctx);

            ADD_HOOK_FUNCTION(float_set, ctx);
            ADD_HOOK_FUNCTION(float_multiply, ctx);
            ADD_HOOK_FUNCTION(float_mulratio, ctx);
            ADD_HOOK_FUNCTION(float_negate, ctx);
            ADD_HOOK_FUNCTION(float_compare, ctx);
            ADD_HOOK_FUNCTION(float_sum, ctx);
            ADD_HOOK_FUNCTION(float_sto, ctx);
            ADD_HOOK_FUNCTION(float_sto_set, ctx);
            ADD_HOOK_FUNCTION(float_invert, ctx);
            ADD_HOOK_FUNCTION(float_mantissa, ctx);
            ADD_HOOK_FUNCTION(float_exponent, ctx);

            ADD_HOOK_FUNCTION(float_divide, ctx);
            ADD_HOOK_FUNCTION(float_one, ctx);
            ADD_HOOK_FUNCTION(float_mantissa, ctx);
            ADD_HOOK_FUNCTION(float_mantissa_set, ctx);
            ADD_HOOK_FUNCTION(float_exponent, ctx);
            ADD_HOOK_FUNCTION(float_exponent_set, ctx);
            ADD_HOOK_FUNCTION(float_sign, ctx);
            ADD_HOOK_FUNCTION(float_sign_set, ctx);
            ADD_HOOK_FUNCTION(float_int, ctx);

            ADD_HOOK_FUNCTION(otxn_burden, ctx);
            ADD_HOOK_FUNCTION(otxn_generation, ctx);
            ADD_HOOK_FUNCTION(otxn_field_txt, ctx);
            ADD_HOOK_FUNCTION(otxn_field, ctx);
            ADD_HOOK_FUNCTION(otxn_id, ctx);
            ADD_HOOK_FUNCTION(otxn_type, ctx);
            ADD_HOOK_FUNCTION(otxn_slot, ctx);
            ADD_HOOK_FUNCTION(hook_account, ctx);
            ADD_HOOK_FUNCTION(hook_hash, ctx);
            ADD_HOOK_FUNCTION(fee_base, ctx);
            ADD_HOOK_FUNCTION(ledger_seq, ctx);
            ADD_HOOK_FUNCTION(ledger_last_hash, ctx);
            ADD_HOOK_FUNCTION(nonce, ctx);

            ADD_HOOK_FUNCTION(hook_param, ctx);
            ADD_HOOK_FUNCTION(hook_param_set, ctx);
            ADD_HOOK_FUNCTION(hook_skip, ctx);
            ADD_HOOK_FUNCTION(hook_pos, ctx);

            ADD_HOOK_FUNCTION(state, ctx);
            ADD_HOOK_FUNCTION(state_foreign, ctx);
            ADD_HOOK_FUNCTION(state_set, ctx);
            ADD_HOOK_FUNCTION(state_foreign_set, ctx);

            ADD_HOOK_FUNCTION(slot, ctx);
            ADD_HOOK_FUNCTION(slot_clear, ctx);
            ADD_HOOK_FUNCTION(slot_count, ctx);
            ADD_HOOK_FUNCTION(slot_id, ctx);
            ADD_HOOK_FUNCTION(slot_set, ctx);
            ADD_HOOK_FUNCTION(slot_size, ctx);
            ADD_HOOK_FUNCTION(slot_subarray, ctx);
            ADD_HOOK_FUNCTION(slot_subfield, ctx);
            ADD_HOOK_FUNCTION(slot_type, ctx);
            ADD_HOOK_FUNCTION(slot_float, ctx);

            ADD_HOOK_FUNCTION(trace, ctx);
            ADD_HOOK_FUNCTION(trace_slot, ctx);
            ADD_HOOK_FUNCTION(trace_num, ctx);
            ADD_HOOK_FUNCTION(trace_float, ctx);

            WasmEdge_TableInstanceContext* hostTable = WasmEdge_TableInstanceCreate(tableType);
            WasmEdge_ImportObjectAddTable(importObj, tableName, hostTable);
            WasmEdge_MemoryInstanceContext* hostMem  = WasmEdge_MemoryInstanceCreate(memType);
            WasmEdge_ImportObjectAddMemory(importObj, memName, hostMem);
        }

        ~HookExecutor()
        {
            WasmEdge_ImportObjectDelete(importObj);
        };
    };

}

#endif
