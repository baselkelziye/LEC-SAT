// Learning model: one step is one rising edge; initial state is explicit.
module counter #(parameter integer N = 4) (
    input wire clk,
    input wire enable,
    output reg [N-1:0] q,
    output wire bad
);
    initial q = {N{1'b0}};

    always @(posedge clk)
        if (enable)
            q <= q + 1'b1;

    // Deliberately false safety property: q must never equal 3.
    assign bad = (q == 3);

`ifdef NATIVE_ASSERT
    // State-based immediate assertion: no extra clocked monitor register.
    always @* begin
        assert(q != 3);
    end
`endif
endmodule
