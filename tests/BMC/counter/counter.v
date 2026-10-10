// Learning model: one step is one rising edge; initial state is explicit.
module counter (
    input wire clk,
    input wire enable,
    output reg [1:0] q,
    output wire bad
);
    initial q = 2'b00;

    always @(posedge clk)
        if (enable)
            q <= q + 2'd1;

    // Deliberately false safety property: q must never equal 3.
    assign bad = (q == 2'b11);

// `ifdef NATIVE_ASSERT
//     // State-based immediate assertion: no extra clocked monitor register.
//     always @* begin
//         assert(q != 2'b11);
//     end
// `endif
endmodule
