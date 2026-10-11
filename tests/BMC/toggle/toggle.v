module toggle #(parameter integer N = 4)(input wire clk, output reg [N-1:0] q);
    initial q = {N{1'b0}};
    always @(posedge clk) q <= ~q;
endmodule
