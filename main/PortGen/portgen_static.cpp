#include "portgen_static.hpp"

#include "../Utils/gravity_utility.hpp"
#include "../Utils/Common.h"


namespace V3::PortGen {

    std::string GenerateMasterPortName(int coreId, std::string name)
    {
        return Gravity::StringAppender()
            .Append("master_port_", coreId, "_0_", name)
            .ToString();
    }

    std::string GenerateMasterULPortName(int coreId, int portId, std::string name)
    {
        return Gravity::StringAppender()
            .Append("master_ul_port_", coreId, "_", portId, "_0_", name)
            .ToString();
    }

    std::string GenerateMasterMPortName(int coreId, int portId, std::string name)
    {
        return Gravity::StringAppender()
            .Append("master_m_port_", coreId, "_", portId, "_0_", name)
            .ToString();
    }

    std::string GenerateDataMPortName(int portId, std::string name)
    {
        return Gravity::StringAppender()
            .Append("matrix_data_out_", portId, "_", name)
            .ToString();
    }
    static void GenerateInfo(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" uint64_t GetCoreCount() { return ", coreCount, "; }").EndLine();
        cpp_file.Append("extern \"C\" uint64_t GetULPortCountPerCore() { return ", tlULPerCore, "; }").EndLine();
        cpp_file.Append("extern \"C\" uint64_t GetMPortCountPerCore() { return ", tlMPerCore, "; }").EndLine();
        cpp_file.EndLine();
    }

#   define PushMasterPort(coreId, lname, rname) \
        cpp_file.Append("    port->", lname, " = verilated->", GenerateMasterPortName(coreId, rname), ";").EndLine()

#   define PushULMasterPort(coreId, portId, lname, rname) \
        cpp_file.Append("    port->", lname, " = verilated->", GenerateMasterULPortName(coreId, portId, rname), ";").EndLine()

#   define PushMMasterPort(coreId, portId, lname, rname) \
        cpp_file.Append("    port->", lname, " = verilated->", GenerateMasterMPortName(coreId, portId, rname), ";").EndLine()

#   define PullMasterPort(coreId, lname, rname) \
        cpp_file.Append("    verilated->", GenerateMasterPortName(coreId, lname), " = port->", rname, ";").EndLine()

#   define PullULMasterPort(coreId, portId, lname, rname) \
        cpp_file.Append("    verilated->", GenerateMasterULPortName(coreId, portId, lname), " = port->", rname, ";").EndLine()

#   define PullMMasterPort(coreId, portId, lname, rname) \
        cpp_file.Append("    verilated->", GenerateMasterMPortName(coreId, portId, lname), " = port->", rname, ";").EndLine()

    static void GeneratePushChannelA(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" void PushChannelA(VTestTop* verilated, TLSequencer* tltest)").EndLine();
        cpp_file.Append("{").EndLine();
        cpp_file.Append("    TLSequencer::IOPort* port;").EndLine();
        cpp_file.EndLine();
        for (int i = 0; i < coreCount; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PushMasterPort(i, "a.ready", "a_ready");
                cpp_file.EndLine();
            }

            for (int j = 0; j < tlULPerCore; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + 1 + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PushULMasterPort(i, j, "a.ready", "a_ready");
                cpp_file.EndLine();
            }

            for (int j = 0; j < tlMPerCore; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + 2 + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PushMMasterPort(i, j, "a.ready", "a_ready");
                cpp_file.EndLine();
            }
        }
        cpp_file.Append("}").EndLine();
        cpp_file.EndLine();
    }

    static void GeneratePullChannelA(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" void PullChannelA(VTestTop* verilated, TLSequencer* tltest)").EndLine();
        cpp_file.Append("{").EndLine();
        cpp_file.Append("    TLSequencer::IOPort* port;").EndLine();
        cpp_file.EndLine();
        for (int i = 0; i < coreCount; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PullMasterPort(i, "a_valid"             , "a.valid");
                PullMasterPort(i, "a_bits_opcode"       , "a.opcode");
                PullMasterPort(i, "a_bits_param"        , "a.param");
                PullMasterPort(i, "a_bits_size"         , "a.size");
                PullMasterPort(i, "a_bits_source"       , "a.source");
                PullMasterPort(i, "a_bits_address"      , "a.address");
                PullMasterPort(i, "a_bits_user_alias"   , "a.alias");
                PullMasterPort(i, "a_bits_mask"         , "a.mask");
                cpp_file.Append("    std::memcpy(",
                        "verilated->", GenerateMasterPortName(i, "a_bits_data"), ", "
                        "port->a.data->data, ",
                        BEATSIZE,
                    ");").EndLine();
                cpp_file.Append("    verilated->", GenerateMasterPortName(i, "a_bits_corrupt"), " = 0;").EndLine();
                cpp_file.EndLine();
            }

            for (int j = 0; j < tlULPerCore; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + 1 + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PullULMasterPort(i, j, "a_valid"            , "a.valid");
                PullULMasterPort(i, j, "a_bits_opcode"      , "a.opcode");
                PullULMasterPort(i, j, "a_bits_param"       , "a.param");
                PullULMasterPort(i, j, "a_bits_size"        , "a.size");
                PullULMasterPort(i, j, "a_bits_source"      , "a.source");
                PullULMasterPort(i, j, "a_bits_address"     , "a.address");
                PullULMasterPort(i, j, "a_bits_mask"        , "a.mask");
                cpp_file.Append("    std::memcpy(",
                        "verilated->", GenerateMasterULPortName(i, j, "a_bits_data"), ", "
                        "port->a.data->data, ",
                        BEATSIZE,
                    ");").EndLine();
                cpp_file.Append("    verilated->", GenerateMasterULPortName(i, j, "a_bits_corrupt"), " = 0;").EndLine();
            }

            for (int j = 0; j < tlMPerCore; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + 2 + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PullMMasterPort(i, j, "a_valid"            , "a.valid");
                PullMMasterPort(i, j, "a_bits_opcode"      , "a.opcode");
                PullMMasterPort(i, j, "a_bits_param"       , "a.param");
                PullMMasterPort(i, j, "a_bits_size"        , "a.size");
                PullMMasterPort(i, j, "a_bits_source"      , "a.source");
                PullMMasterPort(i, j, "a_bits_address"     , "a.address");
                PullMMasterPort(i, j, "a_bits_mask"        , "a.mask");
                PullMMasterPort(i, j,"a_bits_user_matrix"  , "a.matrix");
                cpp_file.Append("    std::memcpy(",
                        "verilated->", GenerateMasterMPortName(i, j, "a_bits_data"), ", "
                        "port->a.data->data, ",
                        BEATSIZE,
                    ");").EndLine();
                cpp_file.Append("    verilated->", GenerateMasterMPortName(i, j, "a_bits_corrupt"), " = 0;").EndLine();
            }
        }
        cpp_file.Append("}").EndLine();
        cpp_file.EndLine();
    }

    static void GeneratePushChannelB(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" void PushChannelB(VTestTop* verilated, TLSequencer* tltest)").EndLine();
        cpp_file.Append("{").EndLine();
        cpp_file.Append("    TLSequencer::IOPort* port;").EndLine();
        cpp_file.EndLine();
        for (int i = 0; i < coreCount; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PushMasterPort(i, "b.valid"     , "b_valid");
                PushMasterPort(i, "b.opcode"    , "b_bits_opcode");
                PushMasterPort(i, "b.param"     , "b_bits_param");
                PushMasterPort(i, "b.size"      , "b_bits_size");
                PushMasterPort(i, "b.source"    , "b_bits_source");
                PushMasterPort(i, "b.address"   , "b_bits_address");
                PushMasterPort(i, "b.alias"     , "b_bits_data[0]");
                cpp_file.EndLine();
            }
        }
        cpp_file.Append("}").EndLine();
        cpp_file.EndLine();
    }

    static void GeneratePullChannelB(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" void PullChannelB(VTestTop* verilated, TLSequencer* tltest)").EndLine();
        cpp_file.Append("{").EndLine();
        cpp_file.Append("    TLSequencer::IOPort* port;").EndLine();
        cpp_file.EndLine();
        for (int i = 0; i < coreCount; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PullMasterPort(i, "b_ready", "b.ready");
                cpp_file.EndLine();
            }
        }
        cpp_file.Append("}").EndLine();
        cpp_file.EndLine();
    }

    static void GeneratePushChannelC(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" void PushChannelC(VTestTop* verilated, TLSequencer* tltest)").EndLine();
        cpp_file.Append("{").EndLine();
        cpp_file.Append("    TLSequencer::IOPort* port;").EndLine();
        cpp_file.EndLine();
        for (int i = 0; i < coreCount; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PushMasterPort(i, "c.ready", "c_ready");
                cpp_file.EndLine();
            }

            // for (int j = 0; j < tlULPerCore; j++)
            // {
            //     int deviceId = i * (1 + tlULPerCore + tlMPerCore) + 1 + j;

            //     cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
            //     PushULMasterPort(i, j, "c.ready", "c_ready");
            //     cpp_file.EndLine();
            // }
        }
        cpp_file.Append("}").EndLine();
        cpp_file.EndLine();
    }

    static void GeneratePullChannelC(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" void PullChannelC(VTestTop* verilated, TLSequencer* tltest)").EndLine();
        cpp_file.Append("{").EndLine();
        cpp_file.Append("    TLSequencer::IOPort* port;").EndLine();
        cpp_file.EndLine();
        for (int i = 0; i < coreCount; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PullMasterPort(i, "c_valid"         , "c.valid");
                PullMasterPort(i, "c_bits_opcode"   , "c.opcode");
                PullMasterPort(i, "c_bits_param"    , "c.param");
                PullMasterPort(i, "c_bits_size"     , "c.size");
                PullMasterPort(i, "c_bits_source"   , "c.source");
                PullMasterPort(i, "c_bits_address"  , "c.address");
                cpp_file.Append("    verilated->", GenerateMasterPortName(i, "c_bits_user_alias"), " = 0;").EndLine();
                cpp_file.Append("    std::memcpy(",
                        "verilated->", GenerateMasterPortName(i, "c_bits_data"), ",",
                        "port->c.data->data, ",
                        BEATSIZE,
                    ");").EndLine();
                cpp_file.Append("    verilated->", GenerateMasterPortName(i, "c_bits_corrupt"), " = 0;").EndLine();
                cpp_file.EndLine();
            }

            // for (int j = 0; j < tlULPerCore; j++)
            // {
            //     int deviceId = i * (1 + tlULPerCore + tlMPerCore) + 1 + j;

            //     cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
            //     PullULMasterPort(i, j, "c_valid"        , "c.valid");
            //     PullULMasterPort(i, j, "c_bits_opcode"  , "c.opcode");
            //     PullULMasterPort(i, j, "c_bits_param"   , "c.param");
            //     PullULMasterPort(i, j, "c_bits_size"    , "c.size");
            //     PullULMasterPort(i, j, "c_bits_source"  , "c.source");
            //     PullULMasterPort(i, j, "c_bits_address" , "c.address");
            //     cpp_file.Append("    verilated->", GenerateMasterULPortName(i, j, "c_bits_user_alias"), " = 0;").EndLine();
            //     cpp_file.Append("    std::memcpy(",
            //             "verilated->", GenerateMasterULPortName(i, j, "c_bits_data"), ",",
            //             "port->c.data->data, ",
            //             BEATSIZE,
            //         ");").EndLine();
            //     cpp_file.Append("    verilated->", GenerateMasterULPortName(i, j, "c_bits_corrupt"), " = 0;").EndLine();
            //     cpp_file.EndLine();
            // }
        }
        cpp_file.Append("}").EndLine();
        cpp_file.EndLine();
    }

    static void GeneratePushChannelD(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" void PushChannelD(VTestTop* verilated, TLSequencer* tltest)").EndLine();
        cpp_file.Append("{").EndLine();
        cpp_file.Append("    TLSequencer::IOPort* port;").EndLine();
        cpp_file.EndLine();
        for (int i = 0; i < coreCount; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PushMasterPort(i, "d.valid"     , "d_valid");
                PushMasterPort(i, "d.opcode"    , "d_bits_opcode");
                PushMasterPort(i, "d.param"     , "d_bits_param");
                PushMasterPort(i, "d.size"      , "d_bits_size");
                PushMasterPort(i, "d.source"    , "d_bits_source");
                PushMasterPort(i, "d.sink"      , "d_bits_sink");
                PushMasterPort(i, "d.denied"    , "d_bits_denied");
                cpp_file.Append("    std::memcpy(",
                        "port->d.data->data, ",
                        "verilated->", GenerateMasterPortName(i, "d_bits_data"), ", ",
                        BEATSIZE,
                    ");").EndLine();
                PushMasterPort(i, "d.corrupt"   , "d_bits_corrupt");
                cpp_file.EndLine();
            }

            for (int j = 0; j < tlULPerCore; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + 1 + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PushULMasterPort(i, j, "d.valid"     , "d_valid");
                PushULMasterPort(i, j, "d.opcode"    , "d_bits_opcode");
                PushULMasterPort(i, j, "d.param"     , "d_bits_param");
                PushULMasterPort(i, j, "d.size"      , "d_bits_size");
                PushULMasterPort(i, j, "d.source"    , "d_bits_source");
                PushULMasterPort(i, j, "d.sink"      , "d_bits_sink");
                PushULMasterPort(i, j, "d.denied"    , "d_bits_denied");
                cpp_file.Append("    std::memcpy(",
                        "port->d.data->data, ",
                        "verilated->", GenerateMasterULPortName(i, j, "d_bits_data"), ", ",
                        BEATSIZE,
                    ");").EndLine();
                PushULMasterPort(i, j, "d.corrupt"   , "d_bits_corrupt");
                cpp_file.EndLine();
            }

            for (int j = 0; j < tlMPerCore; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + 2 + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PushMMasterPort(i, j, "d.valid"     , "d_valid");
                PushMMasterPort(i, j, "d.opcode"    , "d_bits_opcode");
                PushMMasterPort(i, j, "d.param"     , "d_bits_param");
                PushMMasterPort(i, j, "d.size"      , "d_bits_size");
                PushMMasterPort(i, j, "d.source"    , "d_bits_source");
                PushMMasterPort(i, j, "d.sink"      , "d_bits_sink");
                PushMMasterPort(i, j, "d.denied"    , "d_bits_denied");
                cpp_file.Append("    std::memcpy(",
                        "port->d.data->data, ",
                        "verilated->", GenerateMasterMPortName(i, j, "d_bits_data"), ", ",
                        BEATSIZE,
                    ");").EndLine();
                PushMMasterPort(i, j, "d.corrupt"   , "d_bits_corrupt");
                cpp_file.EndLine();

                cpp_file.Append("    std::memcpy(",
                        "port->m.data->data, ",
                        "verilated->", GenerateDataMPortName(j,"bits_data_data"), ", ",
                        DATASIZE,
                    ");").EndLine();
                cpp_file.Append("    port->", "m.source", " = verilated->",
                        GenerateDataMPortName(j, "bits_sourceId"), ";")
                    .EndLine();
                cpp_file.Append("    port->", "m.valid", " = verilated->",
                        GenerateDataMPortName(j, "valid"), ";")
                    .EndLine();
                cpp_file.Append("    port->", "m.ready", " = verilated->",
                        GenerateDataMPortName(j, "ready"), ";")
                    .EndLine();
                cpp_file.EndLine();
            }
        }
        cpp_file.Append("}").EndLine();
        cpp_file.EndLine();
    }

    static void GeneratePullChannelD(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" void PullChannelD(VTestTop* verilated, TLSequencer* tltest)").EndLine();
        cpp_file.Append("{").EndLine();
        cpp_file.Append("    TLSequencer::IOPort* port;").EndLine();
        cpp_file.EndLine();
        for (int i = 0; i < coreCount; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PullMasterPort(i, "d_ready", "d.ready");
                cpp_file.EndLine();
            }

            for (int j = 0; j < tlULPerCore; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + 1 + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PullULMasterPort(i, j,  "d_ready", "d.ready");
                cpp_file.EndLine();
            }

            for (int j = 0; j < tlMPerCore; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + 2 + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PullMMasterPort(i, j,  "d_ready", "d.ready");
                cpp_file.Append("    verilated->", GenerateDataMPortName(j, "ready"),
                        " = port->", "m.ready", ";").EndLine();
                cpp_file.EndLine();
            }
        }
        cpp_file.Append("}").EndLine();
        cpp_file.EndLine();
    }

    static void GeneratePushChannelE(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" void PushChannelE(VTestTop* verilated, TLSequencer* tltest)").EndLine();
        cpp_file.Append("{").EndLine();
        cpp_file.Append("    TLSequencer::IOPort* port;").EndLine();
        cpp_file.EndLine();
        for (int i = 0; i < coreCount; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PushMasterPort(i, "e.ready", "e_ready");
                cpp_file.EndLine();
            }
        }
        cpp_file.Append("}").EndLine();
        cpp_file.EndLine();
    }

    static void GeneratePullChannelE(Gravity::StringAppender& cpp_file, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        cpp_file.Append("extern \"C\" void PullChannelE(VTestTop* verilated, TLSequencer* tltest)").EndLine();
        cpp_file.Append("{").EndLine();
        cpp_file.Append("    TLSequencer::IOPort* port;").EndLine();
        cpp_file.EndLine();
        for (int i = 0; i < coreCount; i++)
        {
            for (int j = 0; j < 1; j++)
            {
                int deviceId = i * (1 + tlULPerCore + tlMPerCore) + j;

                cpp_file.Append("    port = &(tltest->IO(", deviceId, "));").EndLine();
                PullMasterPort(i, "e_valid"     , "e.valid");
                PullMasterPort(i, "e_bits_sink" , "e.sink");
                cpp_file.EndLine();
            }
        }
        cpp_file.Append("}").EndLine();
        cpp_file.EndLine();
    }

    //
    std::string Generate(int coreCount, int tlULPerCore, int tlMPerCore)
    {
        //
        Gravity::StringAppender cpp_file;

        cpp_file.Append("#include \"VTestTop.h\"").EndLine();
        cpp_file.Append("#include \"Sequencer/TLSequencer.hpp\"").EndLine();
        cpp_file.Append("#include <cstring>").EndLine();
        cpp_file.Append("#include <cstdint>").EndLine();
        cpp_file.EndLine();

        GenerateInfo(cpp_file, coreCount, tlULPerCore, tlMPerCore);

        GeneratePushChannelA(cpp_file, coreCount, tlULPerCore, tlMPerCore);
        GeneratePullChannelA(cpp_file, coreCount, tlULPerCore, tlMPerCore);

        GeneratePushChannelB(cpp_file, coreCount, tlULPerCore, tlMPerCore);
        GeneratePullChannelB(cpp_file, coreCount, tlULPerCore, tlMPerCore);

        GeneratePushChannelC(cpp_file, coreCount, tlULPerCore, tlMPerCore);
        GeneratePullChannelC(cpp_file, coreCount, tlULPerCore, tlMPerCore);

        GeneratePushChannelD(cpp_file, coreCount, tlULPerCore, tlMPerCore);
        GeneratePullChannelD(cpp_file, coreCount, tlULPerCore, tlMPerCore);

        GeneratePushChannelE(cpp_file, coreCount, tlULPerCore, tlMPerCore);
        GeneratePullChannelE(cpp_file, coreCount, tlULPerCore, tlMPerCore);

        //
        return cpp_file.ToString();
    }
}