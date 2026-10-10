module toggle(input wire clk, output reg q);
    initial q = 0;
    always @(posedge clk) q <= ~q;
endmodule
